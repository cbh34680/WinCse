#include "WinCseLib.h"
#include "CSDevice.hpp"
#include <iostream>

using namespace CSELIB;

void t_WinCseLib_aws_s3_FEP(std::initializer_list<std::function<void(ICSDevice*)>> callbacks);

static void printDirEntryList(ICSDevice* device, const std::optional<ObjectKey> optParent, const DirEntryListType& dirEntryList)
{
    for (auto& dirEntry: dirEntryList)
    {
        const auto objKey{ optParent ? optParent->append(dirEntry->mName) : *ObjectKey::fromObjectPath(dirEntry->mName) };
        const auto objKeyFileType = objKey.toFileType();

        const auto dirEntryFileType = dirEntry->mFileType;
        APP_ASSERT(dirEntryFileType == objKeyFileType);

        std::wcout << objKey.str() << L" ";

        if (objKey.isBucket())
        {
            APP_ASSERT(objKeyFileType == FileTypeEnum::Bucket);

            std::wcout << L"[Bucket] ";
        }
        else
        {
            if (objKey.meansDir())
            {
                APP_ASSERT(objKeyFileType == FileTypeEnum::Directory);

                std::wcout << L"[Dir] ";
            }
            else if (objKey.meansFile())
            {
                APP_ASSERT(objKeyFileType == FileTypeEnum::File);

                std::wcout << L"[File] ";
            }
            else
            {
                APP_ASSERT(0);
            }
        }

        if (objKey.meansHidden())
        {
            std::wcout << L"{hidden} ";
        }

        std::wcout << std::endl;

        if (objKey.meansDir())
        {
            DirEntryListType subDirEntryList;

            if (!device->listObjects(START_CALLER objKey, &subDirEntryList))
            {
                std::wcerr << L"fault: listDisplayObjects" << std::endl;
                return;
            }

            printDirEntryList(device, objKey, subDirEntryList);
        }
    }
}

static void listStart(ICSDevice* device)
{
    DirEntryListType buckets;

    if (!device->listBuckets(START_CALLER &buckets))
    {
        std::wcerr << L"fault: listBuckets" << std::endl;
    }

    printDirEntryList(device, std::nullopt, buckets);

    std::wcout << L"done." << std::endl;
}

void t_WinCseLib_aws_s3_Find()
{
    t_WinCseLib_aws_s3_FEP({ listStart });
}

// EOF