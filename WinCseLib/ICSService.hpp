#pragma once
#pragma warning(push)
#pragma warning(disable: 4100)

namespace WCSE {

struct ICSService
{
	virtual ~ICSService() = default;

	virtual bool PreCreateFilesystem(FSP_SERVICE *Service, PCWSTR argWorkDir, FSP_FSCTL_VOLUME_PARAMS* VolumeParams) { return true; };
	virtual bool OnSvcStart(PCWSTR argWorkDir, FSP_FILE_SYSTEM* FileSystem, PCWSTR PtfsPath) { return true; };
	virtual void OnSvcStop() { };
};

} // namespace WCSE

#pragma warning(pop)
// EOF