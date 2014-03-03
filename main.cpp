/*
 * main.cpp
 *
 * Dyplo example application.
 *
 * (C) Copyright 2013,2014 Topic Embedded Products B.V. (http://www.topic.nl).
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
/* This file is compiled twice, once with and once without HAVE_HARDWARE
 * defined. The version without HAVE_HARDWARE can run on any posix
 * system (e.g. a PC), the hardware enabled version needs the Dyplo
 * logic and driver present. */
#include "dyplo/hardware.hpp"
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <string.h>
#include <ctype.h>


#include "softwareprocesses.hpp"

#include "dyplo/threadedprocess.hpp"
#include "dyplo/cooperativescheduler.hpp"
#include "dyplo/cooperativeprocess.hpp"
#include "dyplo/thread.hpp"
#ifdef HAVE_HARDWARE
#	include "dyplo/hardware.hpp"
#	include "dyplo/filequeue.hpp"
#endif

template <class T, int raise, int blocksize> void process_block_add_constant(T* dest, T* src)
{
	for (int i = 0; i < blocksize; ++i)
		*dest++ = (*src++) + raise;
}

void process_string_to_int(int* dest, std::string *src)
{
	std::string src_digits = "";

	/* Loop to remove non-digit characters */
	for ( int i = 0; i < src->size(); ++i )
	{
		if(isdigit(src->at(i)))
		{
			src_digits += src->at(i);
		}
	}

	*dest = atoi(src_digits.c_str());
}

void process_int_to_string(std::string *dest, int* src)
{
	std::stringstream ss;
	ss << *src;
	*dest = ss.str();
}

void display_string(std::string* src)
{
	std::cout << *src << std::endl;
}

int main(int argc, char** argv)
{
	try
	{
#ifdef HAVE_HARDWARE
		/* Create objects for hardware control */
		dyplo::HardwareContext hardware;
		dyplo::HardwareControl hwControl(hardware);
		dyplo::FilePollScheduler hardwareScheduler;
		/* Assume that all bitstreams are partial streams. */
		hardware.setProgramMode(true);
		/* One or more bitstream filenames can be provided on the
		 * commandline. We load them here, no data is present in the
		 * system yet, so replacing logic is still safe to do here. */
		for (int arg_ind = 1; arg_ind < argc; ++arg_ind)
		{
			hardware.program(argv[arg_ind]);
		}
		hardware.setProgramMode(false);
#endif
		/* Create the queues first, because they need to exist before
		 * the processes are started, and at least for as long as the
		 * processes run. */
		dyplo::FixedMemoryQueue<std::string, dyplo::PthreadScheduler> q_input_strings(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_input_ints(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_to_left_adder(2);
#ifdef HAVE_HARDWARE
		dyplo::File f_to_right_adder(hardware.openFifo(0, O_WRONLY));
		dyplo::FileOutputQueue<int, true> q_to_right_adder(hardwareScheduler, f_to_right_adder, 16);
		dyplo::File f_from_left_adder(hardware.openFifo(1, O_WRONLY));
		dyplo::FileOutputQueue<int, true> q_from_left_adder(hardwareScheduler, f_from_left_adder, 16);
		dyplo::File f_from_joining_adder(hardware.openFifo(0, O_RDONLY));
		dyplo::FileInputQueue<int> q_from_joining_adder(hardwareScheduler, f_from_joining_adder, 16);
#else
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_to_right_adder(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_from_left_adder(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_from_right_adder(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_from_joining_adder(2);
#endif
		dyplo::FixedMemoryQueue<std::string, dyplo::PthreadScheduler> q_to_output(2);

		/* Create the processes */
		dyplo::ThreadedProcess<
			typeof(q_input_strings), typeof(q_input_ints),
			process_string_to_int,
			1 /*blocksize*/ > p_string_to_int;

		TeeProcess<typeof(q_input_ints), typeof(q_to_left_adder), typeof(q_to_right_adder)>
			p_tee;

		dyplo::ThreadedProcess<
			typeof(q_to_left_adder), typeof(q_from_left_adder),
			process_block_add_constant<int, 5, 1>
			> p_left_adder;

#ifdef HAVE_HARDWARE
		/* Hardware processes don't need CPU resources, but they often
		 * need configuration. The simplest method is to just write to
		 * the configuration "file", the data will be send via the AXI
		 * bus to the offsets corresponding to the file position. */
		{
			int right_adder_constant = 3;
			dyplo::File cfg(hardware.openConfig(1, O_WRONLY));
			cfg.write(&right_adder_constant, sizeof(right_adder_constant));
		}
#else
		dyplo::ThreadedProcess<
			typeof(q_to_right_adder), typeof(q_from_right_adder),
			process_block_add_constant<int, 3, 1>
			> p_right_adder;
			
		JoiningAddProcess<typeof(q_from_left_adder),
			typeof(q_from_right_adder), typeof(q_from_joining_adder)
			> p_joining_adder;
#endif

		dyplo::ThreadedProcess<
			typeof(q_from_joining_adder), typeof(q_to_output),
			process_int_to_string,
			1 /*blocksize*/ > p_int_to_string;

		ThreadedProcessSink<
			typeof(q_to_output),
			display_string,
			1 /*blocksize*/ > p_display_string;
		
		/* Connect the processes and queues from output to input */
		p_display_string.set_input(&q_to_output);
		p_int_to_string.set_output(&q_to_output);
		p_int_to_string.set_input(&q_from_joining_adder);
#ifdef HAVE_HARDWARE
		hwControl.routeAddSingle(0, 0, 1, 0); /* CPU node Fifo 0 to node 1 fifo 0*/
		hwControl.routeAddSingle(1, 0, 2, 0); /* Node 1 fifo 0 to node 2 fifo 0*/
		hwControl.routeAddSingle(0, 1, 2, 1); /* CPU node Fifo 1 to node 2 fifo 1 */
		hwControl.routeAddSingle(2, 0, 0, 0); /* Node 2 to CPU node fifo 0 */
#else
		p_joining_adder.set_output(&q_from_joining_adder);
		p_joining_adder.set_input_left(&q_from_left_adder);
		p_joining_adder.set_input_right(&q_from_right_adder);
		p_right_adder.set_output(&q_from_right_adder);
		p_right_adder.set_input(&q_to_right_adder);
#endif
		p_left_adder.set_output(&q_from_left_adder);
		p_left_adder.set_input(&q_to_left_adder);
		p_tee.set_output_left(&q_to_left_adder);
		p_tee.set_output_right(&q_to_right_adder);
		p_tee.set_input(&q_input_ints);
		p_string_to_int.set_output(&q_input_ints);
		p_string_to_int.set_input(&q_input_strings);
		
		/* Loop reading the input until end of file. Note that output
		 * is not handled correctly, the program will simply 'abort'
		 * and data present in the processing pipeline may be lost. */
		for(;;)
		{
			std::string line;
			std::cin >> line;
			if (std::cin.eof())
				break;
			q_input_strings.push_one(line);
		}
	}
	catch (const std::exception& ex)
	{
		std::cerr << "ERROR:\n" << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
