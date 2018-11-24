/**
 * Non-metric Space Library
 *
 * Authors: Bilegsaikhan Naidan (https://github.com/bileg), Leonid Boytsov (http://boytsov.info).
 * With contributions from Lawrence Cayton (http://lcayton.com/) and others.
 *
 * For the complete list of contributors and further details see:
 * https://github.com/searchivarius/NonMetricSpaceLib
 *
 * Copyright (c) 2010--2013
 *
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 */

#ifndef PIVOT_NEIGHBORHOOD_HORDER_HASHPIV_INVINDEX_H
#define PIVOT_NEIGHBORHOOD_HORDER_HASHPIV_INVINDEX_H

#include <vector>
#include <mutex>
#include <memory>

#include "index.h"
#include "permutation_utils.h"
#include "ported_boost_progress.h"

#define METH_PIVOT_NEIGHB_HORDER_HASHPIV_INVINDEX      "napp_horder_hashpiv"

//#define SINGLE_MUTEX_FLUSH

#include <method/pivot_neighb_common.h>
#include <method/pivot_neighb_horder_common.h>

#include "vector_pool.h"

namespace similarity {

using std::vector;
using std::mutex;
using std::unique_ptr;

/*
 * A modified variant of the Neighborhood-APProximation Index (NAPP):
 * more details to follow.
 *
 */

template <typename dist_t>
class PivotNeighbHorderHashPivInvIndex : public Index<dist_t> {
 public:
  PivotNeighbHorderHashPivInvIndex(bool PrintProgress,
                           const Space<dist_t>& space,
                           const ObjectVector& data);

  virtual void CreateIndex(const AnyParams& IndexParams) override;
  virtual void SaveIndex(const string &location) override;
  virtual void LoadIndex(const string &location) override;

  ~PivotNeighbHorderHashPivInvIndex();

  const std::string StrDesc() const override;
  void Search(RangeQuery<dist_t>* query, IdType) const override;
  void Search(KNNQuery<dist_t>* query, IdType) const override;
  void SetQueryTimeParams(const AnyParams& QueryTimeParams) override;
 private:

  const   Space<dist_t>&  space_;
  bool    PrintProgress_;

  size_t  K_;
  size_t  knn_amp_;
  float   db_scan_frac_;
  size_t  num_prefix_;       // K in the original paper
  size_t  num_prefix_search_;// K used during search (our modification can use a different K)
  size_t  min_times_;        // t in the original paper
  bool    skip_checking_;
  size_t  index_thread_qty_;
  size_t  num_pivot_;
  string  pivot_file_;
  bool    disable_pivot_index_;
  int     print_pivot_stat_;
  size_t hash_trick_dim_;
  unsigned pivot_comb_qty_;

  unique_ptr<PivotIndex<dist_t>> pivot_index_;

  enum eAlgProctype {
    kMerge,
    kScan,
    kPriorQueue,
    kStoreSort
  } inv_proc_alg_;

  string toString(eAlgProctype type) const {
    if (type == kScan)   return PERM_PROC_FAST_SCAN;
    if (type == kPriorQueue) return PERM_PROC_PRIOR_QUEUE;
    if (type == kMerge)  return PERM_PROC_MERGE;
    if (type == kStoreSort) return PERM_PROC_STORE_SORT;
    return "unknown";
  }

  ObjectVector    pivot_;
  vector<IdType>  pivot_pos_;

  ObjectVector    genPivot_; // generated pivots

  void initPivotIndex() {
    if (disable_pivot_index_) {
      pivot_index_.reset(new DummyPivotIndex<dist_t>(space_, pivot_));
      LOG(LIB_INFO) << "Created a dummy pivot index";
    } else {
      pivot_index_.reset(space_.CreatePivotIndex(pivot_, hash_trick_dim_));
      LOG(LIB_INFO) << "Attempted to create an efficient pivot index (however only few spaces support such index)";
    }
  }

  #define ADD_CHECKS

  inline IdTypeUnsign PostingListIndex(IdTypeUnsign pivotId1,
                                      IdTypeUnsign pivotId2) const {
   if (pivotId1 > pivotId2) {
     swap(pivotId1, pivotId2);
   }
#ifdef ADD_CHECKS
   CHECK(pivotId1 != pivotId2 );
   CHECK(pivotId2 < num_pivot_ );
#endif

   IdTypeUnsign res = pivotId1 + pivotId2*(pivotId2 - 1) / 2 ;

#ifdef ADD_CHECKS
   static IdTypeUnsign resUB = num_pivot_ * (num_pivot_ - 1) / 2;

   CHECK(res < resUB); 
#endif
      
   return res;
  }

  inline IdTypeUnsign PostingListIndex(IdTypeUnsign pivotId1,
                                      IdTypeUnsign pivotId2,
                                      IdTypeUnsign pivotId3) const {
  IdTypeUnsign pivots[3] = {pivotId1, pivotId2, pivotId3};
  sort(pivots, pivots + 3);

  pivotId1 = pivots[0];
  pivotId2 = pivots[1];
  pivotId3 = pivots[2];

#ifdef ADD_CHECKS
   CHECK(pivotId1 < pivotId2 && pivotId2 < pivotId3 && pivotId3 < num_pivot_);
#endif

   IdTypeUnsign res = pivotId1 + 
                      pivotId2 * (pivotId2 - 1) / 2 + 
                      pivotId3 * (pivotId3-1) * (pivotId3-2) / 6;

#ifdef ADD_CHECKS
   static IdTypeUnsign resUB = num_pivot_ * (num_pivot_ - 1) * (num_pivot_ - 2) / 6;

   CHECK(res < resUB); 
#endif
      
   return res;
  }


  size_t                                        maxPostQty_;
  vector<unique_ptr<PostingListHorderType>>     posting_lists_;
  #ifndef SINGLE_MUTEX_FLUSH
  vector<mutex*>                                post_list_mutexes_;
  #else
  mutex                                         post_list_single_mutex_;
  #endif

  vector<unique_ptr<vector<PostingListHorderType>>>   tmp_posting_lists_;
  vector<size_t>                                      tmp_post_doc_qty_;

  unique_ptr<VectorPool<IdType>>          tmp_res_pool_;
  unique_ptr<VectorPool<const Object*>>   cand_pool_;
  unique_ptr<VectorPool<unsigned>>        counter_pool_;
  unique_ptr<VectorPool<uint32_t>>        combId_pool_;

  size_t exp_post_per_query_qty_ = 0;
  size_t exp_avg_post_size_ = 0;

  mutex                           progress_bar_mutex_;
  unique_ptr<ProgressDisplay>     progress_bar_;

  size_t getPostQtysOnePivot(size_t skipVal) const {
    return (skipVal - 1 + size_t(num_pivot_)) / skipVal;
  }

  size_t getPostQtysTwoPivots(size_t skipVal) const {
    return (skipVal - 1 + size_t(num_pivot_) * (num_pivot_ - 1)/ 2) / skipVal;
  }

  size_t getPostQtysThreePivots(size_t skipVal) const {
    CHECK(num_pivot_ >= 2);
    return (skipVal - 1 + size_t(num_pivot_) * size_t(num_pivot_ - 1) * size_t(num_pivot_ - 2)/ 6) / skipVal;
  }

  size_t getPostQtys(unsigned pivotCombQty, size_t skipVal) const {
    CHECK_MSG(pivotCombQty && pivotCombQty <= 3,
              "Illegal number of pivots in the combinations " + ConvertToString(pivotCombQty) + " must be >0 and <=3");
    if (pivotCombQty == 1) return getPostQtysOnePivot(skipVal);
    if (pivotCombQty == 2) return getPostQtysTwoPivots(skipVal);
    CHECK(pivotCombQty == 3);
    return getPostQtysThreePivots(skipVal);
  }

  template <typename QueryType> void GenSearch(QueryType* query, size_t K) const;

  void GetPermutationPPIndexEfficiently(const Object* object, Permutation& p) const;
  void GetPermutationPPIndexEfficiently(const Query<dist_t>* query, Permutation& p) const;
  void GetPermutationPPIndexEfficiently(Permutation &p, const vector <dist_t> &vDst) const;

  // disable copy and assign
  DISABLE_COPY_AND_ASSIGN(PivotNeighbHorderHashPivInvIndex);

  void GetPermutationPPIndexEfficiently(Permutation &p, const vector<bool> &vDst) const;


  /*
   * Returns the actual number of stored IDs. To reduce number of reallocations,
   * we will only increase the size of the vector with ids, but never shrink them.
   * This way, ids vector can be reused among calls. However, we cannot clear
   * a vector container by calling clear(), b/c it doesn't guarantee
   * to retain vector's capacity.
   */
  size_t genPivotCombIds(std::vector<uint32_t>& ids, const Permutation& perm, unsigned permPrefix) const;


  void flushTmpPost(unsigned threadId) {
    CHECK(threadId <= tmp_posting_lists_.size());


    tmp_post_doc_qty_[threadId] = 0;
    vector<PostingListHorderType>& tmpAllPivList = (*tmp_posting_lists_[threadId]);
#ifndef SINGLE_MUTEX_FLUSH
    for (IdType pivId = 0; pivId < maxPostQty_; ++pivId) {
      {
        CHECK(pivId < post_list_mutexes_.size());
        unique_lock <mutex> lock(*post_list_mutexes_[pivId]);

        PostingListInt& permList = *posting_lists_[pivId];
        PostingListInt& tmpList = tmpAllPivList[pivId];

        size_t oldSize = permList.size();
        size_t addSize = tmpList.size();
        permList.resize(oldSize + addSize);
        memcpy(&permList[oldSize], &tmpList[0], sizeof(tmpList[0]) * addSize);
        // Don't forget to clear the temporary buffer!
        // It doesn't free the memory though: https://en.cppreference.com/w/cpp/container/vector/clear
        tmpList.clear();
      }
    }
#else
    {
      unique_lock<mutex> lock(post_list_single_mutex_);
      for (IdType pivId = 0; pivId < maxPostQty_; ++pivId) {

        PostingListInt& permList = *posting_lists_[pivId];
        PostingListInt& tmpList = tmpAllPivList[pivId];

        size_t oldSize = permList.size();
        size_t addSize = tmpList.size();
        permList.resize(oldSize + addSize);
        memcpy(&permList[oldSize], &tmpList[0], sizeof(tmpList[0]) * addSize);
        // Don't forget to clear the temporary buffer!
        // It doesn't free the memory though: https://en.cppreference.com/w/cpp/container/vector/clear
        tmpList.clear();
      }
    }
#endif
  }

  mutable size_t  post_qty_ = 0;
  mutable size_t  search_time_ = 0;
  mutable size_t  dist_comp_time_ = 0;
  mutable size_t  dist_pivot_comp_time_ = 0;
  mutable size_t  sort_comp_time_ = 0;
  mutable size_t  copy_post_time_ = 0;
  mutable size_t  scan_sorted_time_ = 0;
  mutable size_t  ids_gen_time_ = 0;
  mutable size_t  proc_query_qty_ = 0;

  mutable mutex   stat_mutex_;

  size_t  skip_val_ = 1;
};

}  // namespace similarity

#endif     // _PERMUTATION_SUCCINCT_INDEX_H_

