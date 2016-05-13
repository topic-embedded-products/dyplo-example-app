/*
 * dyploexamplezdma.cpp
 *
 * Dyplo example application for zero-copy DMA transfers.
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
 * into a single output stream, and uses DMA in Zero-copy mode for the data
 * transfers.
 *
 * Using the zero-copy mode makes the code more complex, but instead of a
 * memory-copy operation from the application's buffer into the DMA buffer, it
 * allows the application direct access to the DMA buffers.
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
		std::string filename = hardware.findPartition(function_name, 2);
		dyplo::HardwareConfig joiningAdderCfg(hardware, 2);
		joiningAdderCfg.disableNode();
                hwControl.program(filename.c_str());
		joiningAdderCfg.enableNode();

		// We'll need three DMA channels: Two to feed the adder, and one to read the
		// results.

		/* The DMA handles must be mapped into memory. Since there is no
		 * such thing as "write-only" memory, you have to open the DMA
		 * handles in RDWR node even though you will only be writing
		 * to it. */
		dyplo::HardwareDMAFifo to_adder_left(hardware.openDMA(0, O_RDWR));
		dyplo::HardwareDMAFifo to_adder_right(hardware.openDMA(1, O_RDWR));
		dyplo::HardwareDMAFifo from_adder(hardware.openDMA(0, O_RDONLY));

		// Set up the routes
		int joiningAdderId = joiningAdderCfg.getNodeIndex();
		// If there are multiple queues, add "q<<8" to the index to
		// address them.
		to_adder_left.addRouteTo(joiningAdderId + (0 << 8));
		to_adder_right.addRouteTo(joiningAdderId + (1 << 8));
		from_adder.addRouteFrom(joiningAdderId);

		/* Allocate buffers, because of the zero-copy system, the driver
		 * will allocate them for us in DMA capable memory, and give us
		 * direct access through a memory map. The library does all the
		 * work for us. */
		static const unsigned int samples_per_block = 4096;
		static const unsigned int bytes_per_block = samples_per_block * sizeof(int);
		static const unsigned int num_blocks = 2;
		to_adder_left.reconfigure(dyplo::HardwareDMAFifo::MODE_COHERENT, bytes_per_block, num_blocks, false);
		to_adder_right.reconfigure(dyplo::HardwareDMAFifo::MODE_COHERENT, bytes_per_block, num_blocks, false);
		from_adder.reconfigure(dyplo::HardwareDMAFifo::MODE_COHERENT, bytes_per_block, num_blocks, true);

		/* Prime the reader with empty blocks. Just dequeue all blocks
		 * and enqueue them. */
		for (unsigned int i = 0; i < num_blocks; ++i)
		{
			dyplo::HardwareDMAFifo::Block *block = from_adder.dequeue();
			block->bytes_used = bytes_per_block;
			from_adder.enqueue(block);
		}

		/* Start data transfer on the senders. To send data, first dequeue
		 * a block from the DMA, fill it with data and set the "bytes_used"
		 * member. Then enqueue it. Do not access the buffer after that! */
		for (unsigned int i = 0; i < num_blocks; ++i)
		{
			dyplo::HardwareDMAFifo::Block *block = to_adder_left.dequeue();
			fillBuffer((int*)block->data, i * 1000, samples_per_block);
			block->bytes_used = bytes_per_block;
			to_adder_left.enqueue(block);
		}
		for (unsigned int i = 0; i < num_blocks; ++i)
		{
			dyplo::HardwareDMAFifo::Block *block = to_adder_right.dequeue();
			fillBuffer((int*)block->data, i * (-1000), samples_per_block);
			block->bytes_used = bytes_per_block;
			to_adder_right.enqueue(block);
		}

		/* Fetch the data. The dequeue operation will block until data
		 * is ready to be retrieved. */
		for (unsigned int b = 0; b < num_blocks; ++b)
		{
			dyplo::HardwareDMAFifo::Block *block = from_adder.dequeue();

			int *data = (int*)block->data;
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

			/* If more data were to be retrieved, the block can be
			 * re-enqueued to the DMA driver. */
			// block->bytes_used = bytes_per_block;
			// from_adder.enqueue(block);
		}

		std::cerr << "OK: " << bytes_per_block * num_blocks << " bytes processed\n";
	}
	catch (const std::exception& ex)
	{
		std::cerr << "ERROR:\n" << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
