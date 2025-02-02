/**
 * @file table_page.cpp
 * @author sheep
 * @brief implementation of table page
 * @version 0.1
 * @date 2022-05-09
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "storage/page/table_page.h"
#include "common/logger.h"

// TODO: logic really need to be re-examined here

namespace TinyDB {

void TablePage::Init(page_id_t page_id, uint32_t page_size, page_id_t prev_page_id, TransactionContext *txn, LogManager *log_manager) {
    SetPageId(page_id);

    // TODO: loggging this

    // we are double-linked list
    // and we are at the tail of the list
    SetPrevPageId(prev_page_id);
    SetNextPageId(INVALID_PAGE_ID);
    // pointing to the end of the page
    SetFreeSpacePointer(page_size);
    SetTupleCount(0);
}

bool TablePage::InsertTuple(const Tuple &tuple, RID *rid, TransactionContext *txn, LogManager *log_manager) {
    TINYDB_ASSERT(tuple.GetSize() > 0, "you shouldn't insert empty tuple");

    // check whether we can store this tuple
    if (GetFreeSpaceRemaining() < tuple.GetSize()) {
        return false;
    }

    // try to find a free slot to reuse
    // in case gcc will re-fetch over and over again
    uint32_t tuple_cnt = GetTupleCount();
    uint32_t slot_id = tuple_cnt;
    for (uint32_t i = 0; i < tuple_cnt; i++) {
        // check whether the slot is empty
        if (GetTupleSize(i) == 0) {
            slot_id = i;
            break;
        }
    }

    // If there is no more empty slot, then we need to recheck whether there is space left
    // for us to store tuple and one more slot
    if (slot_id == tuple_cnt && GetFreeSpaceRemaining() < tuple.GetSize() + SIZE_SLOT) {
        return false;
    }

    // update free space pointer
    SetFreeSpacePointer(GetFreeSpacePointer() - tuple.GetSize());
    // serialize it into the page
    tuple.SerializeTo(GetRawPointer() + GetFreeSpacePointer());

    // then update the slot pointer and size
    SetTupleOffset(slot_id, GetFreeSpacePointer());
    SetTupleSize(slot_id, tuple.GetSize());

    // set rid
    if (rid != nullptr) {
        rid->Set(GetPageId(), slot_id);
    }

    // if we are creating new slot, then we need to update tuple cnt
    if (slot_id == tuple_cnt) {
        SetTupleCount(tuple_cnt + 1);
    }

    if (log_manager != nullptr) {
        TINYDB_ASSERT(txn != nullptr, "txn context is null");
        auto log = LogRecord(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::INSERT, RID(GetPageId(), slot_id), tuple);
        auto lsn = log_manager->AppendLogRecord(log);
        SetLSN(lsn);
        txn->SetPrevLSN(lsn);
    }

    return true;
}

bool TablePage::MarkDelete(const RID &rid, TransactionContext *txn, LogManager *log_manager) {
    TINYDB_ASSERT(rid.GetPageId() == GetPageId(), "Wrong page");
    uint32_t slot_id = rid.GetSlotId();
    // check whether slot id is vaild
    if (slot_id >= GetTupleCount()) {
        return false;
    }

    uint32_t tuple_size = GetTupleSize(slot_id);

    // we don't want to delete a empty tuple
    if (tuple_size == 0) {
        return false;
    }

    // we are encountering double marking, i.e. ww-conflict
    // this should be a logic error
    TINYDB_ASSERT(IsDeleted(tuple_size) == false, "Deleting an tuple with deletion mark");

    if (log_manager != nullptr) {
        TINYDB_ASSERT(txn != nullptr, "txn context is null");
        Tuple dummy_tuple;
        auto log = LogRecord(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::MARKDELETE, rid, dummy_tuple);
        auto lsn = log_manager->AppendLogRecord(log);
        SetLSN(lsn);
        txn->SetPrevLSN(lsn);
    }

    SetTupleSize(slot_id, SetDeletedFlag(tuple_size));
    return true;
}

bool TablePage::UpdateTuple(const Tuple &new_tuple, Tuple *old_tuple, const RID &rid, TransactionContext *txn, LogManager *log_manager) {
    TINYDB_ASSERT(rid.GetPageId() == GetPageId(), "Wrong page");
    TINYDB_ASSERT(new_tuple.GetSize() > 0, "cannot insert empty tuples");
    uint32_t slot_id = rid.GetSlotId();
    // check the slot id
    if (slot_id >= GetTupleCount()) {
        return false;
    }

    uint32_t tuple_size = GetTupleSize(slot_id);

    if (tuple_size == 0) {
        return false;
    }

    // this should be a logic error
    // because if we have the ownership of this tuple, we should see either the full tuple
    // or an empty tuple
    TINYDB_ASSERT(IsDeleted(tuple_size) == false, "updating an tuple with deletion mark");

    // check whether we have enough space
    if (GetFreeSpaceRemaining() + tuple_size < new_tuple.GetSize()) {
        return false;
    }

    // copyout the old value
    // should we copyout the value only when pointer is not null?
    uint32_t tuple_offset = GetTupleOffset(slot_id);
    old_tuple->DeserializeFromInplace(GetRawPointer() + tuple_offset, tuple_size);
    old_tuple->SetRID(rid);

    uint32_t free_space_ptr = GetFreeSpacePointer();
    // move the data behind us(physically before us)
    // overlap awaring
    memmove(GetRawPointer() + free_space_ptr + tuple_size - new_tuple.GetSize(),
            GetRawPointer() + free_space_ptr,
            tuple_offset - free_space_ptr);
    // serialize new tuple
    new_tuple.SerializeTo(GetRawPointer() + tuple_offset + tuple_size - new_tuple.GetSize());
    SetTupleSize(slot_id, new_tuple.GetSize());
    SetFreeSpacePointer(free_space_ptr + tuple_size - new_tuple.GetSize());

    // we need to update offsets of tuple that is moved by us
    // note that offset has no correlation with slot id. so we need to 
    // check all of the slots
    uint32_t tuple_cnt = GetTupleCount();
    for (uint32_t i = 0; i < tuple_cnt; i++) {
        uint32_t tuple_offset_i = GetTupleOffset(i);
        // note that the reason we check offset_i < tuple_offset + tuple_size
        // instead of offset_i < tuple_offset is we need to also update the offset 
        // of new tuple.
        // another approach would be update new tuple offset individually. And condition here
        // should be offset_i < tuple_offset && i != slot_id
        if (GetTupleSize(i) != 0 && tuple_offset_i < tuple_offset + tuple_size) {
            SetTupleOffset(i, tuple_offset_i + tuple_size - new_tuple.GetSize());
        }
    }

    if (log_manager != nullptr) {
        TINYDB_ASSERT(txn != nullptr, "txn context is null");
        auto log = LogRecord(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::UPDATE, rid, *old_tuple, new_tuple);
        auto lsn = log_manager->AppendLogRecord(log);
        SetLSN(lsn);
        txn->SetPrevLSN(lsn);
    }
    
    return true;
}

// perform the direct deletion.
void TablePage::ApplyDelete(const RID &rid, TransactionContext *txn, LogManager *log_manager) {
    TINYDB_ASSERT(rid.GetPageId() == GetPageId(), "Wrong page");
    uint32_t slot_id = rid.GetSlotId();
    TINYDB_ASSERT(slot_id < GetTupleCount(), "invalid slot id");

    uint32_t tuple_offset = GetTupleOffset(slot_id);
    uint32_t tuple_size = GetTupleSize(slot_id);
    TINYDB_ASSERT(IsValid(tuple_size), "can not delete an empty tuple");

    if (IsDeleted(tuple_size)) {
        // mask out the delete bit
        tuple_size = UnsetDeletedFlag(tuple_size);
    }

    // copyout the deleted tuple for undo purposes
    if (log_manager != nullptr) {
        TINYDB_ASSERT(txn != nullptr, "txn context is null");
        Tuple deleted_tuple = Tuple::DeserializeFrom(GetRawPointer() + tuple_offset, tuple_size);
        auto log = LogRecord(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::APPLYDELETE, rid, deleted_tuple);
        auto lsn = log_manager->AppendLogRecord(log);
        SetLSN(lsn);
        txn->SetPrevLSN(lsn);
    }

    // move the data
    uint32_t free_space_ptr = GetFreeSpacePointer();
    memmove(GetRawPointer() + free_space_ptr + tuple_size,
            GetRawPointer() + free_space_ptr,
            tuple_offset - free_space_ptr);
    SetTupleSize(slot_id, 0);
    SetTupleOffset(slot_id, 0);
    SetFreeSpacePointer(free_space_ptr + tuple_size);
    
    // update tuple offsets
    auto tuple_cnt = GetTupleCount();
    for (uint32_t i = 0; i < tuple_cnt; i++) {
        auto tuple_offset_i = GetTupleOffset(i);
        if (GetTupleSize(i) != 0 && tuple_offset_i < tuple_offset) {
            SetTupleOffset(i, tuple_offset_i + tuple_size);
        }
    }
}

void TablePage::RollbackDelete(const RID &rid, TransactionContext *txn, LogManager *log_manager) {
    TINYDB_ASSERT(rid.GetPageId() == GetPageId(), "Wrong page");
    // just unset the delete flag
    uint32_t slot_id = rid.GetSlotId();
    TINYDB_ASSERT(slot_id < GetTupleCount(), "invalid slot id");
    uint32_t tuple_size = GetTupleSize(slot_id);

    if (log_manager != nullptr) {
        TINYDB_ASSERT(txn != nullptr, "txn context is null");
        Tuple dummy_tuple;
        auto log = LogRecord(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::ROLLBACKDELETE, rid, dummy_tuple);
        auto lsn = log_manager->AppendLogRecord(log);
        SetLSN(lsn);
        txn->SetPrevLSN(lsn);
    }

    if (IsDeleted(tuple_size)) {
        SetTupleSize(slot_id, UnsetDeletedFlag(tuple_size));
    }
}

bool TablePage::GetTuple(const RID &rid, Tuple *tuple) {
    TINYDB_ASSERT(rid.GetPageId() == GetPageId(), "Wrong page");
    uint32_t slot_id = rid.GetSlotId();
    // should we use assertion here?
    if (slot_id >= GetTupleCount()) {
        return false;
    }

    auto tuple_size = GetTupleSize(slot_id);
    // should we skip this tuple?
    // instead of aborting the txn
    // because in RC isolation level, it's very likely that we will read deleted tuple
    if (IsDeleted(tuple_size)) {
        return false;
    }

    auto tuple_offset = GetTupleOffset(slot_id);
    tuple->DeserializeFromInplace(GetRawPointer() + tuple_offset, tuple_size);
    tuple->SetRID(rid);

    return true;
}

// i think we should only skip those tuple that is really deleted instead of just a mark
// since txn may get aborted, and deletion may fail
bool TablePage::GetFirstTupleRid(RID *first_rid) {
    auto tuple_cnt = GetTupleCount();
    for (uint32_t i = 0; i < tuple_cnt; i++) {
        // find the first valid tuple
        if (!IsDeleted(GetTupleSize(i))) {
            first_rid->Set(GetPageId(), i);
            return true;
        }
    }

    first_rid->Set(INVALID_PAGE_ID, 0);
    return false;
}

bool TablePage::GetNextTupleRid(const RID &cur_rid, RID *next_rid) {
    TINYDB_ASSERT(cur_rid.GetPageId() == GetPageId(), "Wrong page");
    // find the first valid tuple after cur_rid
    auto tuple_cnt = GetTupleCount();
    for (uint32_t i = cur_rid.GetSlotId() + 1; i < tuple_cnt; i++) {
        if (!IsDeleted(GetTupleSize(i))) {
            next_rid->Set(GetPageId(), i);
            return true;
        }
    }

    next_rid->Set(INVALID_PAGE_ID, 0);
    return false;
}

}