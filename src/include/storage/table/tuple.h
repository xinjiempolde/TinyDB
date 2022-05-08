/**
 * @file tuple.h
 * @author sheep
 * @brief tuple
 * @version 0.1
 * @date 2022-05-07
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef TUPLE_H
#define TUPLE_H

#include "common/rid.h"
#include "catalog/schema.h"
#include "type/value.h"

namespace TinyDB {

/**
 * @brief 
 * description of single tuple that stays in memory
 * Tuple format:
 * | FIXED-SIZE VALUE or VARIED-SIZE OFFSET | PAYLOAD OF VARIED-SIZE TYPE
 * i.e. for every column, either it contains the corresponding fixed-size value which can be 
 * retrieved based on column-offset in schema, or it contains the offset of varied-size type, and 
 * the corresponding payload is placed at the end of the tuple
 */
class Tuple {
public:
    // default tuple, which doesn't have any specific data nor the information
    Tuple() = default;

    // create tuple from values and corresponding schema
    Tuple(std::vector<Value> values, const Schema *schema);

    // copy constructor
    Tuple(const Tuple &other);
    Tuple &operator=(Tuple other);

    void Swap(Tuple &rhs) {
        std::swap(rhs.allocated_, allocated_);
        std::swap(rhs.data_, data_);
        std::swap(rhs.size_, size_);
        rid_.Swap(rhs.rid_);
    }

    ~Tuple();

    // helper functions

    inline RID GetRID() const {
        return rid_;
    }
    
    // TODO: should we return const char *?
    inline char *GetData() const {
        return data_;
    }

    /**
     * @brief Get the tuple length, including varlen object
     * @return uint32_t 
     */
    inline uint32_t GetLength() const {
        return size_;
    }

    inline bool IsAllocated() const {
        return allocated_;
    }

    // get the value of a specified column
    Value GetValue(const Schema *schema, uint32_t column_idx) const;

    /**
     * @brief 
     * generate a key tuple given schemas and attributes
     * @param schema schema of current tuple
     * @param key_schema schema of returned tuple
     * @param key_attrs indices of the columns of old schema that will constitute new schema
     * @return Tuple 
     */
    Tuple KeyFromTuple(const Schema *schema, const Schema *key_schema, const std::vector<uint32_t> &key_attrs);

    /**
     * @brief 
     * generate a tuple by giving base schema and target schema. And we will generate key_attrs list ourself
     * @param schema 
     * @param key_schema 
     * @return Tuple 
     */
    Tuple KeyFromTuple(const Schema *schema, const Schema *key_schema);

    // Is the column value null?
    inline bool IsNull(const Schema *schema, uint32_t column_idx) const {
        Value value = GetValue(schema, column_idx);
        return value.IsNull();
    }

    std::string ToString(const Schema *schema) const;

    // serialize tuple data
    void SerializeTo(char *storage) const;

    // deserialize tuple data
    static Tuple DeserializeFrom(const char *storage);

private:
    // get the starting storage address of specific column
    const char *GetDataPtr(const Schema *schema, uint32_t column_idx) const;

    // is tuple allocated?
    // maybe we can get this attribute by checking whether data pointer is null
    bool allocated_{false};
    
    // default is invalid rid
    RID rid_{};

    // total size of this tuple
    uint32_t size_{0};

    // payload
    char *data_{nullptr};
};

}

#endif