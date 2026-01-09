#include <interfaces/types.h>
#include <index/scripttypeindex.h>
#include <common/args.h>
#include <dbwrapper.h>
#include <primitives/block.h>
#include <logging.h>

static constexpr uint8_t DB_BLOCK_STATS{'s'};
static constexpr uint8_t DB_BLOCK_HEIGHT{'h'};

std::unique_ptr<ScriptTypeIndex> g_script_type_index;

ScriptTypeIndex::ScriptTypeIndex(std::unique_ptr<interfaces::Chain> chain, size_t cache_size, bool f_memory, bool f_wipe)
    : BaseIndex(std::move(chain), "scripttypeindex")
    , m_db(std::make_unique<BaseIndex::DB>(gArgs.GetDataDirNet() / "indexes" / "scripttypeindex",
                                           cache_size, /*memory=*/false, /*wipe=*/false))
{
}

ScriptTypeIndex::~ScriptTypeIndex() = default;

bool ScriptTypeIndex::CustomInit(const std::optional<interfaces::BlockRef>& block)
{
    if (!block) {
        // Starting fresh, cumulative arrays already zero-initialized
        return true;
    }

    // Restore cumulative state from last indexed block
    ScriptTypeBlockStats stats;
    if (!m_db->Read(std::make_pair(DB_BLOCK_STATS, block->hash), stats)) {
        LogError("%s: Failed to read cumulative stats for block %s", __func__, block->hash.ToString());
        return false;
    }

    // set the cumulative stats to the stats from the last indexed block
    m_cumulative_output_counts = stats.output_counts;
    m_cumulative_output_values = stats.output_values;
    m_current_block_hash = block->hash;

    return true;
}

bool ScriptTypeIndex::CustomAppend(const interfaces::BlockInfo& block) {
    assert(block.data);
    // compute stats for current block
    ScriptTypeBlockStats stats = ComputeStats(*block.data);

    // update cumulative stats
    for (size_t i = 0; i < ScriptTypeBlockStats::TXOUT_TYPE_COUNT; i++) {
        m_cumulative_output_counts[i] += stats.output_counts[i];
        m_cumulative_output_values[i] += stats.output_values[i];
    }

    ScriptTypeBlockStats cumulative;
    cumulative.output_counts = m_cumulative_output_counts;
    cumulative.output_values = m_cumulative_output_values;

    // write to db (with batch for performance and atomicity)
    CDBBatch batch(*m_db);
    batch.Write(std::make_pair(DB_BLOCK_STATS, block.hash), cumulative);
    batch.Write(std::make_pair(DB_BLOCK_HEIGHT, block.height), block.hash);

    m_db->WriteBatch(batch);

    m_current_block_hash = block.hash; // update current block hash
    return true;
}

bool ScriptTypeIndex::CustomRemove(const interfaces::BlockInfo& block) {
    assert(block.data);
    
    ScriptTypeBlockStats stats = ComputeStats(*block.data); // compute stats for current block

     // Subtract from cumulative totals with underflow protection
     for (size_t i = 0; i < ScriptTypeBlockStats::TXOUT_TYPE_COUNT; i++) {
        // Check for underflow in output counts
        if (m_cumulative_output_counts[i] < stats.output_counts[i]) {
            LogError("%s: Underflow detected in output_counts[%zu] for block %s: cumulative=%llu, block=%llu",
                     __func__, i, block.hash.ToString(), static_cast<unsigned long long>(m_cumulative_output_counts[i]), static_cast<unsigned long long>(stats.output_counts[i]));
            return false;
        }
        
        // Check for underflow in output values (CAmount is signed, so check it won't go negative)
        if (m_cumulative_output_values[i] < stats.output_values[i]) {
            LogError("%s: Underflow detected in output_values[%zu] for block %s: cumulative=%lld, block=%lld",
                     __func__, i, block.hash.ToString(), static_cast<long long>(m_cumulative_output_values[i]), static_cast<long long>(stats.output_values[i]));
            return false;
        }
        
        m_cumulative_output_counts[i] -= stats.output_counts[i];
        m_cumulative_output_values[i] -= stats.output_values[i];
    }

   // Erase this block's entries
   CDBBatch batch(*m_db);
   batch.Erase(std::make_pair(DB_BLOCK_STATS, block.hash));
   batch.Erase(std::make_pair(DB_BLOCK_HEIGHT, block.height));

   m_db->WriteBatch(batch);
   m_current_block_hash = block.prev_hash ? *block.prev_hash : uint256{};

   return true;
}

bool ScriptTypeIndex::LookupStats(const uint256& block_hash, ScriptTypeBlockStats& stats) const {
    return m_db->Read(std::make_pair(DB_BLOCK_STATS, block_hash), stats);
}

bool ScriptTypeIndex::LookupStatsByHeight(int height, ScriptTypeBlockStats& stats) const
{
    uint256 block_hash;
    if (!m_db->Read(std::make_pair(DB_BLOCK_HEIGHT, height), block_hash)) {
        return false;
    }
    return LookupStats(block_hash, stats);
}
ScriptTypeBlockStats ScriptTypeIndex::ComputeStats(const CBlock& block) const { 
    
    // Init stats
    ScriptTypeBlockStats stats{};

    // loop through all transactions in the block and check every output to determine the script type
    for (const auto& tx :block.vtx) {
        for(const auto& out : tx->vout) {
            std::vector<std::vector<unsigned char>> solutions; // unused, but needed for Solver
            TxoutType script_type = Solver(out.scriptPubKey, solutions);
            size_t type_idx = static_cast<size_t>(script_type);

            // skip unknown script types
            if (type_idx >= ScriptTypeBlockStats::TXOUT_TYPE_COUNT) {
                LogError("ScriptTypeIndex: Unknown script type %d\n", static_cast<int>(script_type));
                continue;
            }

            // update stats
            stats.output_counts[type_idx]++;
            stats.output_values[type_idx] += out.nValue;
        }
    }

    return stats; 
}