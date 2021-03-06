//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.h
//
// Identification: src/include/buffer/buffer_pool_manager.h
//
// Copyright (c) 2015-2019, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <list>  // NOLINT
#include <unordered_map>

#include "buffer/clock_replacer.h"
#include "recovery/log_manager.h"
#include "storage/disk/disk_manager.h"
#include "storage/page/page.h"

namespace bustub {

/**
 * BufferPoolManager reads disk pages to and from its internal buffer pool.
 */
class BufferPoolManager {
 public:
  enum class CallbackType { BEFORE, AFTER };
  using bufferpool_callback_fn = void (*)(enum CallbackType, const page_id_t page_id);

  /**
   * Creates a new BufferPoolManager.
   * @param pool_size the size of the buffer pool
   * @param disk_manager the disk manager
   * @param log_manager the log manager (for testing only: nullptr = disable logging)
   */
  BufferPoolManager(size_t pool_size, DiskManager *disk_manager, LogManager *log_manager = nullptr);

  /**
   * Destroys an existing BufferPoolManager.
   */
  ~BufferPoolManager();

  /** Grading function. Do not modify! */
  Page *FetchPage(page_id_t page_id, bufferpool_callback_fn callback = nullptr) {
    GradingCallback(callback, CallbackType::BEFORE, page_id);
    auto *result = FetchPageImpl(page_id);
    GradingCallback(callback, CallbackType::AFTER, page_id);
    return result;
  }

  /** Grading function. Do not modify! */
  bool UnpinPage(page_id_t page_id, bool is_dirty, bufferpool_callback_fn callback = nullptr) {
    GradingCallback(callback, CallbackType::BEFORE, page_id);
    auto result = UnpinPageImpl(page_id, is_dirty);
    GradingCallback(callback, CallbackType::AFTER, page_id);
    return result;
  }

  /** Grading function. Do not modify! */
  bool FlushPage(page_id_t page_id, bufferpool_callback_fn callback = nullptr) {
    GradingCallback(callback, CallbackType::BEFORE, page_id);
    auto result = FlushPageImpl(page_id);
    GradingCallback(callback, CallbackType::AFTER, page_id);
    return result;
  }

  /** Grading function. Do not modify! */
  Page *NewPage(page_id_t *page_id, bufferpool_callback_fn callback = nullptr) {
    GradingCallback(callback, CallbackType::BEFORE, INVALID_PAGE_ID);
    auto *result = NewPageImpl(page_id);
    GradingCallback(callback, CallbackType::AFTER, *page_id);
    return result;
  }

  /** Grading function. Do not modify! */
  bool DeletePage(page_id_t page_id, bufferpool_callback_fn callback = nullptr) {
    GradingCallback(callback, CallbackType::BEFORE, page_id);
    auto result = DeletePageImpl(page_id);
    GradingCallback(callback, CallbackType::AFTER, page_id);
    return result;
  }

  /** Grading function. Do not modify! */
  void FlushAllPages(bufferpool_callback_fn callback = nullptr) {
    GradingCallback(callback, CallbackType::BEFORE, INVALID_PAGE_ID);
    FlushAllPagesImpl();
    GradingCallback(callback, CallbackType::AFTER, INVALID_PAGE_ID);
  }

  /** @return pointer to all the pages in the buffer pool */
  Page *GetPages() { return pages_; }

  /** @return size of the buffer pool */
  size_t GetPoolSize() { return pool_size_; }

  /** @return the size of page table (Added by Jigao for test case) */
  inline size_t GetPageTableSize() {
    std::shared_lock<std::shared_mutex> lock(global_latch_);
    return page_table_.size();
  }

  /** @return true for the page loaded in buffer pool, otherwise false (Added by Jigao for test case) */
  inline bool FindInBuffer(page_id_t page_id) {
    std::shared_lock<std::shared_mutex> lock(global_latch_);
    return page_table_.find(page_id) != page_table_.end();
  }

  /** @return the pin count of the page id (Added by Jigao for test case) */
  inline int GetPagePinCount(page_id_t page_id) {
    std::shared_lock<std::shared_mutex> lock(global_latch_);
    const auto& got = page_table_.find(page_id);
    assert(got != page_table_.end());
    return (pages_ + got->second)->GetPinCount();
  }

  /** @return the size of replacer (Added by Jigao for test case) */
  inline size_t GetReplacerSize() {
    std::shared_lock<std::shared_mutex> lock(global_latch_);
    return replacer_->Size();
  }

  /** @return the size of free list (Added by Jigao for test case) */
  inline size_t GetFreeListSize() {
    std::shared_lock<std::shared_mutex> lock(global_latch_);
    return free_list_.size();
  }

 private:
  /**
   * Grading function. Do not modify!
   * Invokes the callback function if it is not null.
   * @param callback callback function to be invoked
   * @param callback_type BEFORE or AFTER
   * @param page_id the page id to invoke the callback with
   */
  void GradingCallback(bufferpool_callback_fn callback, CallbackType callback_type, page_id_t page_id) {
    if (callback != nullptr) {
      callback(callback_type, page_id);
    }
  }

  /**
   * Fetch the requested page from the buffer pool.
   * @param page_id id of page to be fetched
   * @return the requested page
   */
  Page *FetchPageImpl(page_id_t page_id);

  /**
   * Unpin the target page from the buffer pool.
   * @param page_id id of page to be unpinned
   * @param is_dirty true if the page should be marked as dirty, false otherwise
   * @return false if the page pin count is <= 0 before this call, true otherwise
   */
  bool UnpinPageImpl(page_id_t page_id, bool is_dirty);

  /**
   * Flushes the target page to disk.
   * @param page_id id of page to be flushed, cannot be INVALID_PAGE_ID
   * @return false if the page could not be found in the page table, true otherwise
   */
  bool FlushPageImpl(page_id_t page_id);

  /**
   * Creates a new page in the buffer pool.
   * @param[out] page_id id of created page
   * @return nullptr if no new pages could be created, otherwise pointer to new page
   */
  Page *NewPageImpl(page_id_t *page_id);

  /**
   * Deletes a page from the buffer pool.
   * @param page_id id of page to be deleted
   * @return false if the page exists but could not be deleted, true if the page didn't exist or deletion succeeded
   */
  bool DeletePageImpl(page_id_t page_id);

  /**
   * Flushes all the pages in the buffer pool to disk.
   */
  void FlushAllPagesImpl();

  /**
   * Evict a page from free list or replacer. Always pick from the free list first.
   * Update select page metadata to contain page_id and add it to the page table.
   * NOT THREAD SAFE, should be called with u_lock locked
   * Precondition: can find one evict page => !free_list_.empty() || replacer_->Size() != 0
   * @param new_page if is called by NewPageImpl
   * @param u_lock Precondition: locked
   * @return the frame where page evicted
   */
  Page *Evict(page_id_t page_id, bool new_page, std::unique_lock<std::shared_mutex>* u_lock);

  /**
   * check if all pages are pinned
   * This function is NOT THREAD SAFE, should be called with protection of mutex
   */
  bool IsAllPinned() {
    for (size_t i = 0; i < pool_size_; i++) {
      Page *const page = pages_ + i;
      if (page->page_id_ != INVALID_PAGE_ID && page->pin_count_ == 0) {
        return false;
      }
    }
    return true;
  }

  /** Number of pages in the buffer pool. */
  size_t pool_size_;
  /** Array of buffer pool pages. */
  Page *pages_;
  /** Pointer to the disk manager. */
  DiskManager *disk_manager_ __attribute__((__unused__));
  /** Pointer to the log manager. */
  LogManager *log_manager_ __attribute__((__unused__));
  /** Page table for keeping track of buffer pool pages. */
  std::unordered_map<page_id_t, frame_id_t> page_table_;
  /** Replacer to find unpinned pages for replacement. */
  Replacer *replacer_;
  /** List of free pages. */
  std::list<frame_id_t> free_list_;
  /** This latch protects buffer manager's shared data structures:
   *  page table, replacer, pages_(the buffer pool), free list */
  std::shared_mutex global_latch_;
};
}  // namespace bustub
