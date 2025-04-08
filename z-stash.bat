cd %~dp0

rd /S /Q stash
md stash

xcopy ..\WinCse stash\WinCse /I /Y /C /E /H
xcopy ..\WinCseLib stash\WinCseLib /I /Y /C /E /H
xcopy ..\WinCse-aws-s3 stash\WinCse-aws-s3 /I /Y /C /E /H

xcopy ..\setup stash\setup /I /Y /C /E /H
xcopy ..\test stash\test /I /Y /C /E /H

xcopy util-SendCtrlBreak stash\util-SendCtrlBreak /I /Y /C /E /H
xcopy util-PrintReportRequest stash\util-PrintReportRequest /I /Y /C /E /H
xcopy util-ClearCacheRequest stash\util-ClearCacheRequest /I /Y /C /E /H

copy *.bat stash
copy ..\*.sln stash

dir stash

rem pause
