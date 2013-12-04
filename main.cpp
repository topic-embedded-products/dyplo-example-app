#include "dyplo/hardware.hpp"
#include <unistd.h>
#include <iostream>
#include <sstream>

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
	*dest = atoi(src->c_str());
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
		dyplo::HardwareContext hardware;
		dyplo::HardwareControl hwControl(hardware);
		dyplo::FilePollScheduler hardwareScheduler;
#endif
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
		hwControl.routeAddSingle(0, 0, 1, 0); /* Fifo 0 to node 1 */
		hwControl.routeAddSingle(1, 0, 2, 0); /* Node 1 to node 2 */
		hwControl.routeAddSingle(0, 1, 2, 1); /* Fifo 1 to node 2 */
		hwControl.routeAddSingle(2, 0, 0, 0); /* Node 2 to fifo 0 */
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
