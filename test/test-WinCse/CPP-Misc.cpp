#include <Windows.h>
#include <iostream>
#include <fstream>
#include <filesystem>

void t_CPP_Misc()
{
	if (::SetEnvironmentVariableA("KEY1", "VAL1"))
	{
		char* key1 = nullptr;
		size_t len;

		if (_dupenv_s(&key1, &len, "KEY1") == 0)
		{
			if (key1)
			{
				std::cout << key1 << std::endl;

				free(key1);
			}
			else
			{
				std::cerr << "error: key1 null" << std::endl;
			}
		}
		else
		{
			std::cerr << "error: _dupenv_s" << std::endl;
		}
	}
	else
	{
		std::cerr << "error: SetEnvironmentVariableA" << std::endl;
	}
}

// EOF