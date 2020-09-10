//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/aggregate_hashtable.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/function/aggregate_function.hpp"

namespace duckdb {
class BoundAggregateExpression;
class BufferManager;
class BufferHandle;

struct AggregateObject {
	AggregateObject(AggregateFunction function, idx_t child_count, idx_t payload_size, bool distinct,
	                PhysicalType return_type)
	    : function(move(function)), child_count(child_count), payload_size(payload_size), distinct(distinct),
	      return_type(return_type) {
	}

	AggregateFunction function;
	idx_t child_count;
	idx_t payload_size;
	bool distinct;
	PhysicalType return_type;

	static vector<AggregateObject> CreateAggregateObjects(vector<BoundAggregateExpression *> bindings);
};

//! SuperLargeHashTable is a linear probing HT that is used for computing
//! aggregates
/*!
    SuperLargeHashTable is a HT that is used for computing aggregates. It takes
   as input the set of groups and the types of the aggregates to compute and
   stores them in the HT. It uses linear probing for collision resolution.
*/
class SuperLargeHashTable {
public:
	SuperLargeHashTable(BufferManager &buffer_manager, idx_t initial_capacity, vector<LogicalType> group_types,
	                    vector<LogicalType> payload_types, vector<BoundAggregateExpression *> aggregates);
	SuperLargeHashTable(BufferManager &buffer_manager, idx_t initial_capacity, vector<LogicalType> group_types,
	                    vector<LogicalType> payload_types, vector<AggregateObject> aggregates);
	~SuperLargeHashTable();

	//! Add the given data to the HT, computing the aggregates grouped by the
	//! data in the group chunk. When resize = true, aggregates will not be
	//! computed but instead just assigned.
	void AddChunk(DataChunk &groups, DataChunk &payload);
	//! Scan the HT starting from the scan_position until the result and group
	//! chunks are filled. scan_position will be updated by this function.
	//! Returns the amount of elements found.
	idx_t Scan(idx_t &scan_position, DataChunk &group, DataChunk &result);

	//! Fetch the aggregates for specific groups from the HT and place them in the result
	void FetchAggregates(DataChunk &groups, DataChunk &result);

	//! Finds or creates groups in the hashtable using the specified group keys. The addresses vector will be filled
	//! with pointers to the groups in the hash table, and the new_groups selection vector will point to the newly
	//! created groups. The return value is the amount of newly created groups.
	idx_t FindOrCreateGroups(DataChunk &groups, Vector &group_hashes, Vector &addresses, SelectionVector &new_groups);
	idx_t FindOrCreateGroups(DataChunk &groups, Vector &addresses, SelectionVector &new_groups);
	void FindOrCreateGroups(DataChunk &groups, Vector &addresses);

	void Combine(SuperLargeHashTable &other);
	void Finalize();

	//! The stringheap of the AggregateHashTable
	StringHeap string_heap;

private:
	BufferManager &buffer_manager;
	//! The aggregates to be computed
	vector<AggregateObject> aggregates;
	//! The types of the group columns stored in the hashtable
	vector<LogicalType> group_types;
	//! The types of the payload columns stored in the hashtable
	vector<LogicalType> payload_types;
	//! The size of the groups in bytes
	idx_t group_width;
	//! The size of the payload (aggregations) in bytes
	idx_t payload_width;

	idx_t hash_width;
	//! The total tuple size
	idx_t tuple_size;
	//! The capacity of the HT. This can be increased using
	//! SuperLargeHashTable::Resize
	idx_t capacity;
	//! The amount of entries stored in the HT currently
	idx_t entries;
	//! The data of the HT
	//! unique_ptr to indicate the ownership
	vector<unique_ptr<BufferHandle>> payload_hds; //! The data of the HT
	vector<data_t *> payload;                     //! The data of the HT
	//! unique_ptr to indicate the ownership
	unique_ptr<BufferHandle> hashes_hdl;
	data_t *hashes;    //! The endptr of the hashtable
	data_ptr_t endptr; // of hashes
	data_ptr_t current_payload_offset_ptr;

	//! The empty payload data
	unique_ptr<data_t[]> empty_payload_data;
	//! Bitmask for getting relevant bits from the hashes to determine the position
	uint64_t bitmask;

	hash_t hash_prefix_remove_bitmask;
	hash_t hash_prefix_get_bitmask;

	const uint8_t hash_prefix_bits = 16;

	vector<unique_ptr<SuperLargeHashTable>> distinct_hashes;
	//
	//	//! The size of the initial flag for each cell
	//	static constexpr int FLAG_SIZE = sizeof(uint8_t);
	//	//! Flag indicating a cell is empty
	//	static constexpr int EMPTY_CELL = 0x00;
	//	//! Flag indicating a cell is full
	//	static constexpr int FULL_CELL = 0xFF;

	SuperLargeHashTable(const SuperLargeHashTable &) = delete;

private:
	//! Resize the HT to the specified size. Must be larger than the current
	//! size.
	void Resize(idx_t size);
	void HashGroups(DataChunk &groups, Vector &addresses);
	void Destroy();
	void CallDestructors(Vector &state_vector, idx_t count);
	void ScatterGroups(DataChunk &groups, unique_ptr<VectorData[]> &group_data, Vector &addresses,
	                   const SelectionVector &sel, idx_t count);

	void Verify();
	void FlushMerge(Vector &source_addresses, Vector &source_hashes, idx_t count);
};

} // namespace duckdb
