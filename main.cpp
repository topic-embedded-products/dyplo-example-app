#include "dyplo/hardware.hpp"
#include <unistd.h>
#include <iostream>

int main(int argc, char** argv)
{
	try
	{
		dyplo::HardwareContext ctrl;
		ctrl.setProgramMode(false);
	}
	catch (const std::exception& ex)
	{
		std::cerr << "ERROR:\n" << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
