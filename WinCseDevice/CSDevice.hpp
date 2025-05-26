#pragma once

#include "CSDeviceBase.hpp"

namespace CSEDVC
{

WINCSEDEVICE_API CSELIB::FILEIO_LENGTH_T writeStreamFromFile(CALLER_ARG
    const std::ostream* argOutputStream,
    const std::filesystem::path& argInputPath, CSELIB::FILEIO_OFFSET_T argInputOffset, CSELIB::FILEIO_LENGTH_T argInputLength);

WINCSEDEVICE_API CSELIB::FILEIO_LENGTH_T writeFileFromStream(CALLER_ARG
    const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOutputOffset,
    const std::istream* argInputStream, CSELIB::FILEIO_LENGTH_T argInputLength);

WINCSEDEVICE_API std::wstring getContentType(CALLER_ARG UINT64 argFileSize, PCWSTR argInputPath, const std::wstring& argKey);

class CSDevice : public CSDeviceBase
{
private:
	WINCSEDEVICE_API bool headObjectOrCache_(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry);

public:
	using CSDeviceBase::CSDeviceBase;

	WINCSEDEVICE_API ~CSDevice();

	WINCSEDEVICE_API bool headBucket(CALLER_ARG const std::wstring& argBucket, CSELIB::DirEntryType* pDirEntry) override;
	WINCSEDEVICE_API bool listBuckets(CALLER_ARG CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSEDEVICE_API bool headObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryType* pDirEntry) override;
	WINCSEDEVICE_API bool listObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSEDEVICE_API bool listDisplayObjects(CALLER_ARG const CSELIB::ObjectKey& argObjKey, CSELIB::DirEntryListType* pDirEntryList) override;
	WINCSEDEVICE_API CSELIB::FILEIO_LENGTH_T getObjectAndWriteFile(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const std::filesystem::path& argOutputPath, CSELIB::FILEIO_OFFSET_T argOffset, CSELIB::FILEIO_LENGTH_T argLength) override;
	WINCSEDEVICE_API bool putObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey, const FSP_FSCTL_FILE_INFO& argFileInfo, PCWSTR argInputPath) override;
	WINCSEDEVICE_API bool copyObject(CALLER_ARG const CSELIB::ObjectKey& argSrcObjKey, const CSELIB::ObjectKey& argDstObjKey) override;
	WINCSEDEVICE_API bool deleteObject(CALLER_ARG const CSELIB::ObjectKey& argObjKey) override;
	WINCSEDEVICE_API bool deleteObjects(CALLER_ARG const std::wstring& argBucket, const std::list<std::wstring>& argKeys) override;
};

}	// namespace CSEDVC

template <typename MetaT>
void setFileInfoFromMetadata(const MetaT& metadata, CSELIB::FILETIME_100NS_T lastModified, const std::string& etag, CSELIB::DirEntryType* pDirEntry)
{
    if (metadata.find("wincse-creation-time") != metadata.cend())
    {
        (*pDirEntry)->mFileInfo.CreationTime = std::stoull(metadata.at("wincse-creation-time"));
    }

    if (metadata.find("wincse-last-access-time") != metadata.cend())
    {
        (*pDirEntry)->mFileInfo.LastAccessTime = std::stoull(metadata.at("wincse-last-access-time"));
    }

    if (metadata.find("wincse-last-write-time") != metadata.cend())
    {
        (*pDirEntry)->mFileInfo.LastWriteTime = std::stoull(metadata.at("wincse-last-write-time"));
    }

    if (metadata.find("wincse-change-time") != metadata.cend())
    {
        (*pDirEntry)->mFileInfo.ChangeTime = std::stoull(metadata.at("wincse-change-time"));
    }

    (*pDirEntry)->mUserProperties.insert({ L"wincse-last-modified", std::to_wstring(lastModified) });
    (*pDirEntry)->mUserProperties.insert({ L"wincse-etag", CSELIB::MB2WC(etag) });
}

template <typename MapT>
void setMetadataFromFileInfo(CALLER_ARG const FSP_FSCTL_FILE_INFO& argFileInfo, MapT* argMap)
{
    NEW_LOG_BLOCK();

    const auto creationTime{ std::to_string(argFileInfo.CreationTime) };
    const auto lastAccessTime{ std::to_string(argFileInfo.LastAccessTime) };
    const auto lastWriteTime{ std::to_string(argFileInfo.LastWriteTime) };
    const auto changeTime{ std::to_string(argFileInfo.ChangeTime) };

    argMap->insert({ "wincse-creation-time",    creationTime });
    argMap->insert({ "wincse-last-access-time", lastAccessTime });
    argMap->insert({ "wincse-last-write-time",  lastWriteTime });
    argMap->insert({ "wincse-change-time",      changeTime });

    traceA("creationTime=%s lastAccessTime=%s lastWriteTime=%s changeTime=%s",
        creationTime.c_str(), lastAccessTime.c_str(), lastWriteTime.c_str(), changeTime.c_str());

#ifdef _DEBUG
    argMap->insert({ "wincse-debug-creation-time",    CSELIB::WinFileTime100nsToLocalTimeStringA(argFileInfo.CreationTime) });
    argMap->insert({ "wincse-debug-last-access-time", CSELIB::WinFileTime100nsToLocalTimeStringA(argFileInfo.LastAccessTime) });
    argMap->insert({ "wincse-debug-last-write-time",  CSELIB::WinFileTime100nsToLocalTimeStringA(argFileInfo.LastWriteTime) });
    argMap->insert({ "wincse-debug-change-time",      CSELIB::WinFileTime100nsToLocalTimeStringA(argFileInfo.ChangeTime) });
#endif
}

// EOF