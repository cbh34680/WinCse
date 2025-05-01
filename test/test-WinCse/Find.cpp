#include "WinCseLib.h"
#include "CSDevice.hpp"
#include <iostream>

using namespace CSELIB;

void t_WinCseLib_aws_s3_FEP(std::initializer_list<std::function<void(CSELIB::ICSDevice*)>> callbacks);

static void printDirInfoList(ICSDevice* device, const std::optional<ObjectKey> optParent, const DirInfoPtrList& dirInfoList)
{
    for (auto& dirInfo: dirInfoList)
    {
        const auto objKey{ optParent ? optParent->append(dirInfo->FileName) : *ObjectKey::fromPath(dirInfo->FileName) };
        const auto objKeyFileType = objKey.toFileType();

        const auto dirInfoFileType = dirInfo->FileType;
        APP_ASSERT(dirInfoFileType == objKeyFileType);

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
                APP_ASSERT(objKeyFileType == FileTypeEnum::DirectoryObject);

                std::wcout << L"[Dir] ";
            }
            else if (objKey.meansFile())
            {
                APP_ASSERT(objKeyFileType == FileTypeEnum::FileObject);

                std::wcout << L"[File] ";
            }
            else
            {
                APP_ASSERT(0);
            }
        }

        if (objKey.isDotEntries())
        {
            std::wcout << L"{dot} ";
        }

        if (objKey.meansHidden())
        {
            std::wcout << L"{hidden} ";
        }

        std::wcout << std::endl;

        if (objKey.meansRegularDir())
        {
            DirInfoPtrList subDirInfoList;

            if (!device->listDisplayObjects(START_CALLER objKey, &subDirInfoList))
            {
                std::wcerr << L"fault: listDisplayObjects" << std::endl;
                return;
            }

            printDirInfoList(device, objKey, subDirInfoList);
        }
    }
}

static void listStart(ICSDevice* device)
{
    DirInfoPtrList buckets;

    if (!device->listBuckets(START_CALLER &buckets))
    {
        std::wcerr << L"fault: listBuckets" << std::endl;
    }

    printDirInfoList(device, std::nullopt, buckets);

    std::wcout << L"done." << std::endl;
}

void t_WinCseLib_aws_s3_Find()
{
    t_WinCseLib_aws_s3_FEP({ listStart });
}

// EOF