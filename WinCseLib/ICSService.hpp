#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WinCseLib {

struct ICSService
{
	virtual ~ICSService() = default;

	virtual bool PreCreateFilesystem(FSP_SERVICE *Service, const wchar_t* argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) { return true; };
	virtual bool OnSvcStart(const wchar_t* argWorkDir, FSP_FILE_SYSTEM *FileSystem) { return true; };
	virtual void OnSvcStop() { };
};

} // namespace WinCseLib

#pragma warning(pop)
// EOF