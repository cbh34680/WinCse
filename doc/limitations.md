# Limitations

This document explains the current limitations.

## Buckets
- **Cannot access buckets in different regions**  
  Buckets in regions different from the authentication credentials cannot be accessed.

- **Bucket creation and deletion are not supported**  
  Operations such as creating or deleting buckets are not available.

- **Hidden files**  
  If a bucket is determined to be in a different region, it is treated as hidden files.

## Objects
- **Handling of directories**  
  Directory creation is virtually achieved by creating empty objects.

- **Renaming directories**  
  Directories can only be renamed if they are empty.

- **Deleting directories**  
  By default, directories can only be deleted if they are empty.

## Explorer
- **Cannot change attributes**  
  Attributes such as read-only cannot be modified. The same applies to security attributes.

- **Hidden files**  
  Files or directories whose names start with "." are treated as hidden files.

## General Operation
- **Use of `abort()`**  
  In unexpected situations, the application may terminate and unmount automatically.  
  During this time, a stack trace may be output to `%windir%\WinCse\`.

- **S3 usage costs**  
  The application executes API calls and stores files in S3 storage.  
  Be mindful of potential costs when handling large file sizes.

- **Verification status**  
  Since this is personally developed, the testing environment is limited, and thorough verification has not been conducted.  
  As a result, be cautious when handling important data.  
  If you encounter issues, please report them via GitHub Issues.
