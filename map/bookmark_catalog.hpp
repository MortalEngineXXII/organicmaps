#pragma once

#include "platform/remote_file.hpp"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class BookmarkCatalog
{
public:
  explicit BookmarkCatalog(std::string const & catalogDir);

  void RegisterDownloadedId(std::string const & id);
  void UnregisterDownloadedId(std::string const & id);

  void Download(std::string const & id, std::string const & name,
                std::function<void()> && startHandler,
                platform::RemoteFile::ResultHandler && finishHandler);
  size_t GetDownloadingCount() const { return m_downloadingIds.size(); }
  std::vector<std::string> GetDownloadingNames() const;

private:
  std::map<std::string, std::string> m_downloadingIds;
  std::set<std::string> m_downloadedIds;
};
