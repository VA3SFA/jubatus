// Jubatus: Online machine learning framework for distributed environment
// Copyright (C) 2011 Preferred Infrastracture and Nippon Telegraph and Telephone Corporation.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#include "inverted_index_storage.hpp"


using namespace std;
using namespace pfi::data;

namespace jubatus {
namespace storage {

inverted_index_storage::inverted_index_storage(){
}

inverted_index_storage::~inverted_index_storage(){
}

void inverted_index_storage::set(const std::string& row, const std::string& column, float val){
  float& v = inv_diff_[row][column2id_.get_id(column)];
  column2norm_diff_[column] -= v * v;
  v = val;
  column2norm_diff_[column] += v * v;
}

float inverted_index_storage::get(const string& row, const string& column) const {
  uint64_t index = column2id_.get_id_const(column);
  if (index == key_manager::NOTFOUND)
    return 0.0;
  {
    tbl_t::const_iterator it = inv_diff_.find(row);
    if (it != inv_diff_.end()) {
      row_t::const_iterator it_row = it->second.find(index);
      if (it_row != it->second.end()) {
        return it_row->second;
      }
    }
  }
  {
    tbl_t::const_iterator it = inv_.find(row);
    if (it != inv_.end()) {
      row_t::const_iterator it_row = it->second.find(index);
      if (it_row != it->second.end()) {
        return it_row->second;
      }
    }
  }
  return 0.0;
}

void inverted_index_storage::remove(const std::string& row, const std::string& column){
  inv_diff_[row][column2id_.get_id(row)] = 0.f;
}

void inverted_index_storage::clear(){
  inv_.clear();
  inv_diff_.clear();
  column2norm_.clear();
  column2norm_diff_.clear();
}

void inverted_index_storage::get_diff(std::string& diff_str) const {
  sparse_matrix_storage diff;
  for (tbl_t::const_iterator it = inv_diff_.begin(); it != inv_diff_.end(); ++it){
    vector<pair<string, float> > columns;
    for (row_t::const_iterator it2 = it->second.begin(); it2 != it->second.end(); ++it2){
      columns.push_back(make_pair(column2id_.get_key(it2->first), it2->second));
    }
    diff.set_row(it->first, columns);
  }

  ostringstream os;
  {
    pfi::data::serialization::binary_oarchive bo(os);
    bo << diff;
    map_float_t column2norm_copied = column2norm_diff_; // TODO remove redundant copy
    bo << column2norm_copied;
  }
  diff_str = os.str(); // TODO remove redudant copy
}

void inverted_index_storage::set_mixed_and_clear_diff(const string& mixed_diff_str){
  istringstream is(mixed_diff_str);
  sparse_matrix_storage mixed_inv;
  map_float_t mixed_column2norm;
  {
    pfi::data::serialization::binary_iarchive bi(is);
    bi >> mixed_inv;
    bi >> mixed_column2norm;
  }

  vector<string> ids;
  mixed_inv.get_all_row_ids(ids);
  for (size_t i = 0; i < ids.size(); ++i){
    const string& row = ids[i];
    row_t& v = inv_[row];
    vector<pair<string, float> > columns;
    mixed_inv.get_row(row, columns);
    for (size_t j = 0; j < columns.size(); ++j){
      size_t id = column2id_.get_id(columns[j].first);
      if (columns[j].second == 0.f){
        v.erase(id);
      } else {
        v[id] = columns[j].second;
      }
    }
  }
  inv_diff_.clear();

  for (map_float_t::const_iterator it = mixed_column2norm.begin(); it != mixed_column2norm.end(); ++it){
    column2norm_[it->first] += it->second;
  }
  column2norm_diff_.clear();
}

void inverted_index_storage::mix(const string& lhs, string& rhs) const{
  sparse_matrix_storage lhs_inv_diff;
  map_float_t lhs_column2norm_diff;
  {
    istringstream is(lhs);
    pfi::data::serialization::binary_iarchive bi(is);
    bi >> lhs_inv_diff;
    bi >> lhs_column2norm_diff;
  }
  sparse_matrix_storage rhs_inv_diff;
  map_float_t rhs_column2norm_diff; 
  {
    istringstream is(rhs);
    pfi::data::serialization::binary_iarchive bi(is);
    bi >> rhs_inv_diff;
    bi >> rhs_column2norm_diff;
  }

  vector<string> ids;
  lhs_inv_diff.get_all_row_ids(ids);
  for (size_t i = 0; i < ids.size(); ++i){
    const string& row = ids[i];
    vector<pair<string, float> > columns;
    lhs_inv_diff.get_row(row, columns);
    rhs_inv_diff.set_row(row, columns);
  }

  for (map_float_t::const_iterator it = lhs_column2norm_diff.begin(); it != lhs_column2norm_diff.end(); ++it){
    rhs_column2norm_diff[it->first] += it->second;
  }

  ostringstream os;
  {
    pfi::data::serialization::binary_oarchive bo(os);
    bo << rhs_inv_diff;
    bo << rhs_column2norm_diff;
  }
  rhs = os.str(); // TODO remove redudant copy
}

bool inverted_index_storage::save(std::ostream& os){ 
  pfi::data::serialization::binary_oarchive oa(os);
  oa << *this;
  return true;
}

bool inverted_index_storage::load(std::istream& is){
  pfi::data::serialization::binary_iarchive ia(is);
  ia >> *this;
  return true;
}

void inverted_index_storage::calc_scores(const sfv_t& query, pfi::data::unordered_map<std::string, float>& scores) const {
  pfi::data::unordered_map<uint64_t, float> i_scores;
  for (size_t i = 0; i < query.size(); ++i){
    const string& fid = query[i].first;
   float val = query[i].second;
    add_inp_scores(fid, val, i_scores);
  }

  for (pfi::data::unordered_map<uint64_t, float>::const_iterator it = i_scores.begin();
       it != i_scores.end(); ++it){
    scores[column2id_.get_key(it->first)] += it->second;
  }
  // the result is unnormalized
}

float inverted_index_storage::calc_columnl2norm(const std::string& row) const{
  float ret = 0.f;
  map_float_t::const_iterator it_diff = column2norm_diff_.find(row);
  if (it_diff != column2norm_diff_.end()) {
    ret += it_diff->second;
  }
  map_float_t::const_iterator it = column2norm_.find(row);
  if (it != column2norm_.end()){
    ret += it->second;
  }
  return ret;
}

void inverted_index_storage::add_inp_scores(const std::string& row, 
                                            float val, 
                                            pfi::data::unordered_map<uint64_t, float>& scores) const{
  pfi::data::unordered_map<uint64_t, float> i_scores;
  tbl_t::const_iterator it_diff = inv_diff_.find(row);
  if (it_diff != inv_diff_.end()){
    const row_t& row_v = it_diff->second;
    for (row_t::const_iterator row_it = row_v.begin(); row_it != row_v.end(); ++row_it){
      i_scores[row_it->first] = row_it->second * val;
    }
  }

  tbl_t::const_iterator it = inv_.find(row);
  if (it != inv_.end()){
    const row_t& row_v = it->second;
    for (row_t::const_iterator row_it = row_v.begin(); row_it != row_v.end(); ++row_it){
      if (i_scores.find(row_it->first) == i_scores.end()){
        i_scores[row_it->first] = row_it->second * val;
      }
    }
  }

  for (pfi::data::unordered_map<uint64_t, float>::const_iterator it = i_scores.begin();
       it != i_scores.end(); ++it){
    scores[it->first] += it->second;
  }
}


std::string inverted_index_storage::name() const{
  return string("inverted_index_storage");
}



}
}