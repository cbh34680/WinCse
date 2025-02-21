# WinCse &middot; Windows Cloud Storage Explorer

WinCse is an application that integrates AWS S3 buckets with Windows Explorer, allowing you to treat S3 buckets as if they were part of your local file system.

## Features
- Displays S3 buckets in Windows Explorer
- Simple interface for easy file management

## Requirements
- Windows 10 or later
- [WinFsp](http://www.secfs.net/winfsp/)
- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp)

## Installation
1. Install [WinFsp](https://winfsp.dev/rel/)
2. Download WinCse (which includes AWS SDK for C++) from the [release](https://github.com/cbh34680/WinCse/releases) page

## Usage
1. Run [setup/install-aws-s3.bat](setup/install-aws-s3.bat) (requires administrator privileges).
2. When the form screen appears, enter your AWS credentials.
3. Press the "Create" button.
4. Execute `mount.bat` from the displayed Explorer directory.
5. Access your S3 buckets in Windows Explorer on the drive selected in the form screen.
6. Execute `un-mount.bat` to disconnect the mounted drive.

## Uninstallation
1. Run `reg-del.bat` to delete the registry information registered with WinFsp (requires administrator privileges).
2. Delete the directory where `*.bat` files are located.
3. If no longer needed, uninstall [WinFsp](https://winfsp.dev/rel/).

## Limitations
- File system editing is not yet supported (planned for future updates).
- Files larger than 4MB are not supported at this time.
- Referencing buckets in different regions fails.
- Only up to 1000 files can be displayed in a single directory.

## Notes
- The current version is in the testing phase.
- Tested only on Windows 11.
- If stable operation is required, we recommend using [Rclone](https://rclone.org/).

## License
This project is licensed under the [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html) and [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
