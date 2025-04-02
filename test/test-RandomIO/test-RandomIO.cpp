// test-RandomIO.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#define _CRT_SECURE_NO_WARNINGS		(1)

#include <iostream>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
	puts("start");

	const char* drive = getenv("WINCSE_MOUNT_DRIVE");
	const char* bucket = getenv("WINCSE_BUCKET_NAME");

	if (drive && bucket)
	{
		// go next
	}
	else
	{
		puts("check environment variables");
		return EXIT_FAILURE;
	}

	if (argc != 3)
	{
		printf("Usage: %s path operation\n", argv[0]);
		return EXIT_FAILURE;
	}

	// よくわからないが、bat の引数で "y:\..." と渡すと実行前に
	// ファイルが存在しないと言われるので、とりあえず回避

	char path[1024];
	sprintf(path, "%s:\\%s\\%s", drive, bucket, argv[1]);

	const char* oper = argv[2];

	printf("path=%s, operation=%s\n", path, oper);

	FILE* fp = nullptr;
	
	if (strcmp(oper, "create") == 0)
	{
		fp = fopen(path, "wb");
	}
	else if (strcmp(oper, "delete") == 0)
	{
		const auto rc = remove(path);
		printf("rc=%d\n", rc);
	}
	else if (strcmp(oper, "read") == 0)
	{
		fp = fopen(path, "rb");
		if (fp)
		{
			char buff[1024];

			const auto nb = fread(buff, 1, sizeof(buff), fp);
			printf("nb=%zu\n", nb);
		}
	}
	else if (strcmp(oper, "write") == 0)
	{
		fp = fopen(path, "wb");
		if (fp)
		{
			char buff[1024] = "abcde";

			const auto nb = fwrite(buff, 1, strlen(buff), fp);
			printf("nb=%zu\n", nb);
		}
	}
	else if (strcmp(oper, "rw") == 0)
	{
		fp = fopen(path, "r+b");
		if (fp)
		{
			//setvbuf(fp, NULL, _IONBF, 0);

			char buff[1024] = "abcde";

			const auto nb = fwrite(buff, 1, strlen(buff), fp);
			printf("nb=%zu\n", nb);

			const auto rf = fflush(fp);
			printf("rf=%d\n", rf);
		}
	}
	else
	{
		puts("unknown operation");
		return EXIT_FAILURE;
	}

	if (fp)
	{
		fclose(fp);
	}

	puts("done.");

	return EXIT_SUCCESS;
}

// EOF