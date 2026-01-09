#ifndef BITCOIN_INDEX_SCRIPTTYPEINDEX_H
#define BITCOIN_INDEX_SCRIPTTYPEINDEX_H

#include <index/base.h>
#include <script/solver.h>

#include <array>
#include <cstdint>

// Default to not maintain a script type index (we can enable it with the -scripttypeindex flag)
static constexpr bool DEFAULT_SCRIPTTYPEINDEX{false};


/**
 * Statistics for script types per block
 */
struct ScriptTypeBlockStats {
    static constexpr size_t TXOUT_TYPE_COUNT = static_cast<size_t>(TxoutType::WITNESS_UNKNOWN) + 1;

    /** Number of outputs created per script type in the block */
    std::array<uint64_t, TXOUT_TYPE_COUNT> output_counts{};

    /** Total satoshis locked in outputs per script type in the block */
    std::array<CAmount, TXOUT_TYPE_COUNT> output_values{};

    SERIALIZE_METHODS(ScriptTypeBlockStats, obj) {
        // Serialize arrays element by element
        for (size_t i = 0; i < TXOUT_TYPE_COUNT; ++i) {
            READWRITE(obj.output_counts[i]);
        }
        for (size_t i = 0; i < TXOUT_TYPE_COUNT; ++i) {
            READWRITE(obj.output_values[i]);
        }
    }
};

/**
 * ScriptTypeIndex tracks the statistics (number and value) per script type per block
 */
class ScriptTypeIndex final : public BaseIndex
{
private:
    std::unique_ptr<BaseIndex::DB> m_db;

    // Cumulative statistics
    std::array<uint64_t, ScriptTypeBlockStats::TXOUT_TYPE_COUNT> m_cumulative_output_counts{};
    std::array<CAmount, ScriptTypeBlockStats::TXOUT_TYPE_COUNT> m_cumulative_output_values{};

    uint256 m_current_block_hash{};

    ScriptTypeBlockStats ComputeStats(const CBlock& block) const;

protected:
    bool CustomAppend(const interfaces::BlockInfo& block) override;
    bool CustomRemove(const interfaces::BlockInfo& block) override;
    bool CustomInit(const std::optional<interfaces::BlockRef>& block) override;
    bool AllowPrune() const override { return false; }
    BaseIndex::DB& GetDB() const override { return *m_db; }

public:
    explicit ScriptTypeIndex(std::unique_ptr<interfaces::Chain> chain, size_t cache_size, bool f_memory = false, bool f_wipe = false);
    ~ScriptTypeIndex() override;

    bool LookupStats(const uint256& block_hash, ScriptTypeBlockStats& stats) const;
    bool LookupStatsByHeight(int height, ScriptTypeBlockStats& stats) const;
};

extern std::unique_ptr<ScriptTypeIndex> g_script_type_index;

#endif