#ifndef _GEO_LOCATOR_HPP
#define _GEO_LOCATOR_HPP

#include <geo/ilocator.hpp>

#include <maxminddb.h>

namespace geo
{

class DbEntry; ///< Forward declaration

class Locator final : public ILocator
{
private:
    std::weak_ptr<DbEntry> m_weakDbEntry;

    inline std::optional<std::shared_ptr<DbEntry>> getEntry() const
    {
        if (!m_weakDbEntry.expired())
        {
            return m_weakDbEntry.lock();
        }

        return std::nullopt;
    }

    std::string m_cachedIp;              ///< The cached IP address.
    MMDB_lookup_result_s m_cachedResult; ///< The cached lookup result.

    /**
     * @brief Retrieves the entry data for a given dot path.
     * @param path The dot path to retrieve the entry data for.
     * @return A base::RespOrError object containing the entry data or an error message.
     */
    base::RespOrError<MMDB_entry_data_s> getEData(const DotPath& path);

    base::OptError lookup(const std::string& ip, const std::shared_ptr<DbEntry>& dbEntry);

public:
    virtual ~Locator() = default;

    Locator() = delete;
    Locator(const std::shared_ptr<DbEntry>& dbEntry)
        : m_weakDbEntry(dbEntry)
    {
        if (m_weakDbEntry.expired())
        {
            throw std::runtime_error("Cannot build a maxmind locator with an expired db entry");
        }
    }

    base::RespOrError<std::string> getString(const std::string& ip, const DotPath& path) override;
    base::RespOrError<uint32_t> getUint32(const std::string& ip, const DotPath& path) override;
    base::RespOrError<double> getDouble(const std::string& ip, const DotPath& path) override;
    base::RespOrError<json::Json> getAsJson(const std::string& ip, const DotPath& path) override;
};

} // namespace geo
#endif // _GEO_LOCATOR_HPP