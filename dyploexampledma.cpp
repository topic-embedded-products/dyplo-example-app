/*
 * dyploexampledma.cpp
 *
 * Dyplo example application for DMA transfers.
 *
 * (C) Copyright 2015 Topic Embedded Products B.V. (http://www.topic.nl).
 * All rights reserved.
 *
 * This file is part of dyplo-example-app.
 * dyplo-example-app is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * dyplo-example-app is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Dyplo.  If not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA or see <http://www.gnu.org/licenses/>.
 *
 * You can contact Topic by electronic mail via info@topic.nl or via
 * paper mail at the following address: Postbus 440, 5680 AK Best, The Netherlands.
 */

/*
 * This program sets up a hardware-only pipeline that adds two data streams
 * into a single output stream, and uses DMA for the data transfers.
 */
#include "dyplo/hardware.hpp"
#include <unistd.h>
#include <string>
#include <iostream>

// Fill a buffer with numbers seed, seed+1, ..., seed+count-1
static void fillBuffer(int* buffer, int seed, unsigned int count)
{
	for (unsigned int i = 0; i < count; ++i)
		buffer[i] = seed + i;
}

int main(int argc, char** argv)
{
	try
	{
		// Create objects for hardware control
		dyplo::HardwareContext hardware;
		dyplo::HardwareControl hwControl(hardware);

		// set base path where to find the partials bitstreams
		std::string libraryName = "hdl_node_examples";
		std::string bitstreamBasePath = "/usr/share/bitstreams/" + libraryName;
		hardware.setBitstreamBasepath(bitstreamBasePath);
		
		// Program the hardware. See dyplodemoapp.cpp for more details.
		static const char *function_name = "joining_adder";
		hardware.setProgramMode(true);
		std::string filename = hardware.findPartition(function_name, 2);
		dyplo::HardwareConfig joiningAdderCfg(hardware, 2);
		joiningAdderCfg.disableNode();
		hardware.program(filename.c_str());
		joiningAdderCfg.enableNode();

		// We'll need three DMA channels: Two to feed the adder, and one to read the
		// results.
		dyplo::HardwareFifo to_adder_left(hardware.openDMA(0, O_WRONLY));
		dyplo::HardwareFifo to_adder_right(hardware.openDMA(1, O_WRONLY));
		dyplo::HardwareFifo from_adder(hardware.openDMA(0, O_RDONLY));

		// Set up the routes
		int joiningAdderId = joiningAdderCfg.getNodeIndex();
		// If there are multiple queues, add "q<<8" to the index to
		// address them.
		to_adder_left.addRouteTo(joiningAdderId + (0 << 8));
		to_adder_right.addRouteTo(joiningAdderId + (1 << 8));
		from_adder.addRouteFrom(joiningAdderId);

		// Allocate a buffer
		static const unsigned int samples_per_block = 4096;
		static const unsigned int bytes_per_block = samples_per_block * sizeof(int);
		int* data = new int[samples_per_block];

		/* The outgoing DMA will transfer data blocks as we provide them. The
		 * incoming DMA will only trigger when enough data is available. You can
		 * get/set this threshold value if the block size is important. */
		from_adder.setDataTreshold(bytes_per_block);

		fillBuffer(data, 1000, samples_per_block);
		/* Writing to DMA will not block (if there's room in the DMA buffers in the
		 * driver). So we can send a substantial amount of data to the system, and
		 * the FPGA will do its work in the background. */
		to_adder_left.write(data, bytes_per_block);

		fillBuffer(data, -1000, samples_per_block);
		to_adder_right.write(data, bytes_per_block);

		/* Fetch the data. This will block until processing is ready. */
		from_adder.read(data, bytes_per_block);

		// Compare the results to what we expect to get (basically, do
		// the same calculation on the CPU)
		for (unsigned int i = 0; i < samples_per_block; ++i)
		{
			if (data[i] != 2 * (int)i)
			{
				std::cerr << "Data mismatch at " << i <<
					" Expected: " << 2*i <<
					" Actual: " << data[i] <<
					std::endl;
				return 2;
			}
		}

		std::cerr << "OK: " << bytes_per_block << " bytes processed\n";
	}
	catch (const std::exception& ex)
	{
		std::cerr << "ERROR:\n" << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
