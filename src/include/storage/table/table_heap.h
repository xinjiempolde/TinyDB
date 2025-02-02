/**
 * @file table_heap.h
 * @author sheep
 * @brief table heap
 * @version 0.1
 * @date 2022-05-10
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef TABLE_HEAP_H
#define TABLE_HEAP_H

#include "buffer/buffer_pool_manager.h"
#include "storage/page/table_page.h"
#include "storage/table/table_iterator.h"
#include "common/exception.h"
#include "common/result.h"
#include "recovery/log_manager.h"
#include "concurrency/transaction_context.h"

namespace TinyDB {

/**
 * @brief 
 * TableHeap is a doubly-linked list of TablePages. It's the abstraction of heap file 
 * that provide high-level operations with tuple data. e.g. insert tuple without knowing real page
 */
class TableHeap {
    friend class TableIterator;
public:
    ~TableHeap() = default;

    /**
     * @brief 
     * open an existing table heap
     * @param first_page_id id of the first page
     * @param buffer_pool_manager 
     */
    TableHeap(page_id_t first_page_id, BufferPoolManager *buffer_pool_manager, LogManager *log_manager = nullptr):
        buffer_pool_manager_(buffer_pool_manager), log_manager_(log_manager), first_page_id_(first_page_id) {
        TINYDB_ASSERT(first_page_id_ != INVALID_PAGE_ID, "Existing table heap should have at least one page");
    }

    /**
     * @brief 
     * create a new table heap
     * @param buffer_pool_manager 
     * @param txn txn context used to create this table
     * @param log_manager 
     */
    TableHeap(BufferPoolManager *buffer_pool_manager, TransactionContext *txn = nullptr, LogManager *log_manager = nullptr) {
        page_id_t first_page_id = INVALID_PAGE_ID;
        auto new_page = reinterpret_cast<TablePage *> (buffer_pool_manager->NewPage(&first_page_id));
        TINYDB_CHECK_OR_THROW_OUT_OF_MEMORY_EXCEPTION(new_page != nullptr, "");

        new_page->Init(first_page_id, PAGE_SIZE, INVALID_PAGE_ID, txn, log_manager);
        buffer_pool_manager->UnpinPage(first_page_id, true);
        buffer_pool_manager_ = buffer_pool_manager;
        log_manager_ = log_manager;
        first_page_id_ = first_page_id;
    }

    // sheep: is this api necessary?

    /**
     * @brief
     * create a new table heap.
     * @param buffer_pool_manager 
     * @param txn txn context used to create this table
     * @param log_manager 
     * @return TableHeap* pointer to the new table heap. return nullptr when failure
     */
    static TableHeap *CreateNewTableHeap(BufferPoolManager *buffer_pool_manager, TransactionContext *txn = nullptr, LogManager *log_manager = nullptr) {
        page_id_t first_page_id = INVALID_PAGE_ID;
        auto new_page = reinterpret_cast<TablePage *> (buffer_pool_manager->NewPage(&first_page_id));
        TINYDB_CHECK_OR_THROW_OUT_OF_MEMORY_EXCEPTION(new_page != nullptr, "");
        
        new_page->Init(first_page_id, PAGE_SIZE, INVALID_PAGE_ID, txn, log_manager);
        buffer_pool_manager->UnpinPage(first_page_id, true);

        return new TableHeap(first_page_id, buffer_pool_manager, log_manager);
    }

    /**
     * @brief 
     * insert new tuple into table heap
     * @param tuple tuple to be inserted
     * @param rid rid of new tuple
     * @param txn txn context
     * @param callback callback function to be called after the insertion is done. this is used for 
     * 2PL concurrency control protocols since we need to acquire the lock right after we inserted a new tuple
     * @return true when insertion succeed
     */
    Result<> InsertTuple(const Tuple &tuple, RID *rid, TransactionContext *txn = nullptr, const std::function<void(const RID &)> &callback = nullptr);

    /**
     * @brief 
     * mark tuple as deleted
     * @param rid rid of target tuple
     * @param txn txn context
     * @return true when mark succeed
     */
    Result<> MarkDelete(const RID &rid, TransactionContext *txn = nullptr);

    /**
     * @brief 
     * update tuple inplace. return false if current page can't store new tuple.
     * i think it's upper-level's responsibility to perform a deletion followed by an insertion
     * to handle this situation
     * @param tuple new tuple value
     * @param rid target tuple rid
     * @param txn txn context
     * @return true when updation succeed
     */
    Result<> UpdateTuple(const Tuple &tuple, const RID &rid, TransactionContext *txn = nullptr);

    /**
     * @brief 
     * delete the tuple. this will perform real deletion
     * @param rid target tuple rid
     * @param txn txn context
     */
    void ApplyDelete(const RID &rid, TransactionContext *txn = nullptr);

    /**
     * @brief 
     * clear the deletion flag of tuple
     * @param rid target tuple rid
     * @param txn txn context
     */
    void RollbackDelete(const RID &rid, TransactionContext *txn = nullptr);

    /**
     * @brief
     * read the tuple
     * @param rid target tuple rid
     * @param tuple tuple value
     * @return true when reading succeed
     */
    Result<> GetTuple(const RID &rid, Tuple *tuple);

    inline page_id_t GetFirstPageId() const {
        return first_page_id_;
    }

    /**
     * @brief 
     * get the begin iterator of this table
     * @return TableIterator 
     */
    TableIterator Begin();
    
    /**
     * @brief 
     * get the end iterator of this table
     * @return TableIterator 
     */
    TableIterator End();

private:
    BufferPoolManager *buffer_pool_manager_;
    LogManager *log_manager_{nullptr};
    page_id_t first_page_id_{INVALID_PAGE_ID};
};

}

#endif