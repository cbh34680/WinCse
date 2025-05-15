# WinCse &middot; Windows Cloud Storage Explorer

WinCse is an application that integrates AWS S3 buckets into Windows Explorer, allowing you to treat S3 buckets like a local file system.  

## Updates
**2025/5/15 15:34 JST**  
I mistakenly uploaded an old file (`0-250220-2315.zip`) to the [release page](https://github.com/cbh34680/WinCse/releases).  
It has now been corrected to the correct version (`0-250512-1345.zip`).

## Key Features
- Manage files on S3 as if they were on a Windows shared drive.
- Customize the names and number of S3 buckets displayed upon mounting.
- Mount S3 buckets in read-only mode.

## System Requirements
- Recommended: Windows 10 or later
- [WinFsp](http://www.secfs.net/winfsp/)
- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)

## Installation
1. Install [WinFsp](https://winfsp.dev/rel/).
2. Download WinCse (includes AWS SDK for C++) from the [release page](https://github.com/cbh34680/WinCse/releases).

## Usage
1. Run `setup/install-aws-s3.bat` as administrator.
2. When the form screen appears, enter AWS authentication details.
3. Click the **Create** button.
4. Run `mount.bat` from the directory shown in Explorer.
5. You can access the selected drive and S3 bucket from Windows Explorer.
6. Run `un-mount.bat` to unmount the drive.

## Uninstallation
1. Unmount any mounted drives.
2. Run `reg-del.bat` as administrator to delete registry information registered in WinFsp.
3. Delete the directory containing the `*.bat` files.
4. If no longer needed, uninstall [WinFsp](https://winfsp.dev/rel/).

## Updating
1. Unmount any mounted drives.
2. Extract the zip file obtained from the [release page](https://github.com/cbh34680/WinCse/releases).
3. Overwrite the setup and x64 directories in the installation directory.

## Limitations
- Some restrictions can be relaxed by modifying the [configuration file](./doc/conf-example.txt).
- Bucket creation and deletion are not supported.
- `abort()` is used to make bug detection easier, which may result in forced termination.
- For other limitations, refer to [Limitations](./doc/limitations-ja.md).

## Notes
- This software has only been tested on Windows 11. Compatibility with other versions is not guaranteed.

## License
This project is licensed under both [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) and [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
