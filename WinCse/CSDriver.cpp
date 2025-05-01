#include "CSDriver.hpp"

using namespace CSELIB;
using namespace CSEDRV;


CSELIB::ICSDriver* NewCSDriver(PCWSTR argCSDeviceType, PCWSTR argIniSection, CSELIB::NamedWorker argWorkers[], CSELIB::ICSDevice* argCSDevice, WINCSE_DRIVER_STATS* argStats)
{
    std::map<std::wstring, IWorker*> workers;

    if (NamedWorkersToMap(argWorkers, &workers) <= 0)
    {
        return nullptr;
    }

    for (const auto key: { L"delayed", L"timer", })
    {
        if (workers.find(key) == workers.cend())
        {
            return nullptr;
        }
    }

	return new CSDriver{ argCSDeviceType, argIniSection, workers, argCSDevice, argStats };
}

DirInfoPtr CSDriver::getDirInfoByWinPath(CALLER_ARG const std::filesystem::path& argWinPath)
{
	if (argWinPath == L"\\")
	{
		// "\" �ւ̃A�N�Z�X�͎Q�Ɨp�f�B���N�g���̏����

		FSP_FSCTL_FILE_INFO fileInfo;

		const auto ntstatus = GetFileInfoInternal(mRuntimeEnv->DirSecurityRef.handle(), &fileInfo);
		if (!NT_SUCCESS(ntstatus))
		{
			return nullptr;
		}

		return allocBasicDirInfo(L"/", FileTypeEnum::RootDirectory, fileInfo);
	}
	else
	{
		const auto optObjKey{ ObjectKey::fromWinPath(argWinPath) };
		if (!optObjKey)
		{
			return nullptr;
		}

		const auto& objKey{ *optObjKey };

		if (objKey.isBucket())
		{
			// "\bucket" �̃p�^�[��

			DirInfoPtr dirInfo;

			if (mDevice->headBucket(CONT_CALLER objKey.bucket(), &dirInfo))
			{
				return dirInfo;
			}
		}
		else if (objKey.isObject())
		{
			// "\bucket\***" �̃p�^�[��

			// �������O�̃t�@�C���ƃf�B���N�g�������݂����Ƃ��ɁA�f�B���N�g����D�悷�邽��
			// �����̖��O���f�B���N�g���ɕϊ����X�g���[�W�𒲂ׁA���݂��Ȃ��Ƃ��̓t�@�C���Ƃ��Ē��ׂ�

			DirInfoPtr dirInfo;

			if (mDevice->headObject(CONT_CALLER objKey.toDir(), &dirInfo))
			{
				// "\bucket\dir" �̃p�^�[��
				// 
				// �f�B���N�g�����̗p

				return dirInfo;
			}

			if (mDevice->headObject(CONT_CALLER objKey, &dirInfo))
			{
				// "\bucket\dir\file.txt" �̃p�^�[��
				// 
				// �t�@�C�����̗p

				return dirInfo;
			}
		}
		else
		{
			APP_ASSERT(0);
		}
	}

	NEW_LOG_BLOCK();
	traceW(L"not found: argWinPath=%s", argWinPath.c_str());

	return nullptr;
}

// EOF