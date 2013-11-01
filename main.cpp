#include "dyplo/hardware.hpp"
#include <unistd.h>
#include <iostream>
#include <sstream>

#include "dyplo/threadedprocess.hpp"
#include "dyplo/cooperativescheduler.hpp"
#include "dyplo/cooperativeprocess.hpp"
#include "dyplo/thread.hpp"

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


template <class InputQueueClass,
		void(*ProcessItemFunction)(typename InputQueueClass::Element*),
		int blocksize = 1>
class ThreadedProcessSink
{
	protected:
		InputQueueClass *input;
		dyplo::Thread m_thread;
	public:
		typedef typename InputQueueClass::Element InputElement;

		ThreadedProcessSink():
			input(NULL),
			m_thread()
		{
		}

		~ThreadedProcessSink()
		{
			if (input != NULL)
				input->interrupt_read();
			m_thread.join();
		}

		void set_input(InputQueueClass *value)
		{
			input = value;
			if (input)
				start();
		}

		void* process()
		{
			unsigned int count;
			InputElement *src;

			try
			{
				for(;;)
				{
					count = input->begin_read(src, blocksize);
					DEBUG_ASSERT(count >= blocksize, "invalid value from begin_read");
					ProcessItemFunction(src);
					input->end_read(blocksize);
				}
			}
			catch (const dyplo::InterruptedException&)
			{
				//
			}
			return 0;
		}
	private:
		void start()
		{
			m_thread.start(&run, this);
		}

		static void* run(void* arg)
		{
			return ((ThreadedProcessSink*)arg)->process();
		}
};

template <class InputQueueClass,
	class OutputQueueClassLeft, class OutputQueueClassRight,
	int blocksize=1>
	class TeeProcess
{
	protected:
		InputQueueClass *input;
		OutputQueueClassLeft *output_left;
		OutputQueueClassRight *output_right;
		dyplo::Thread m_thread;
	public:
		TeeProcess():
			input(NULL),
			output_left(NULL),
			output_right(NULL),
			m_thread()
		{
		}

		~TeeProcess()
		{
			if (input != NULL)
				input->interrupt_read();
			if (output_left != NULL)
				output_left->interrupt_write();
			if (output_right != NULL)
				output_right->interrupt_write();
			m_thread.join();
		}

		void set_input(InputQueueClass *value)
		{
			input = value;
			try_start();
		}
		void set_output_left(OutputQueueClassLeft *value)
		{
			output_left = value;
			try_start();
		}
		void set_output_right(OutputQueueClassRight *value)
		{
			output_right = value;
			try_start();
		}

		void process()
		{
			for(;;)
			{
				typename InputQueueClass::Element *src;
				input->begin_read(src, blocksize);
				{
					typename OutputQueueClassLeft::Element *dst;
					output_left->begin_write(dst, blocksize);
					for (int i=0; i<blocksize; ++i)
						dst[i] = src[i];
					output_left->end_write(blocksize);
				}
				{
					typename OutputQueueClassRight::Element *dst;
					output_right->begin_write(dst, blocksize);
					for (int i=0; i<blocksize; ++i)
						dst[i] = src[i];
					output_right->end_write(blocksize);
				}
				input->end_read(blocksize);
			}
		}
	private:
		void try_start()
		{
			if (input && output_left && output_right)
				start();
		}
	
		void start()
		{
			m_thread.start(&run, this);
		}

		static void* run(void* arg)
		{
			try
			{
				((TeeProcess*)arg)->process();
			}
			catch (const dyplo::InterruptedException&)
			{
			}
			return NULL;
		}
};

template <class InputQueueClassLeft, class InputQueueClassRight,
	class OutputQueueClass,
	int blocksize=1>
	class JoiningAddProcess
{
	protected:
		InputQueueClassLeft *input_left;
		InputQueueClassRight *input_right;
		OutputQueueClass *output;
		dyplo::Thread m_thread;
	public:
		JoiningAddProcess():
			input_left(NULL),
			input_right(NULL),
			output(NULL),
			m_thread()
		{
		}

		~JoiningAddProcess()
		{
			if (input_left != NULL)
				input_left->interrupt_read();
			if (input_right != NULL)
				input_right->interrupt_read();
			if (output != NULL)
				output->interrupt_write();
			m_thread.join();
		}

		void set_input_left(InputQueueClassLeft *value)
		{
			input_left = value;
			try_start();
		}
		void set_input_right(InputQueueClassRight *value)
		{
			input_right = value;
			try_start();
		}
		void set_output(OutputQueueClass *value)
		{
			output = value;
			try_start();
		}

		void process()
		{
			for(;;)
			{
				typename InputQueueClassLeft::Element *src_left;
				typename InputQueueClassRight::Element *src_right;
				input_left->begin_read(src_left, blocksize);
				input_right->begin_read(src_right, blocksize);
				typename OutputQueueClass::Element *dst;
				output->begin_write(dst, blocksize);
				for (int i=0; i<blocksize; ++i)
					dst[i] = src_left[i] + src_right[i];
				output->end_write(blocksize);
				input_right->end_read(blocksize);
				input_left->end_read(blocksize);
			}
		}
	private:
		void try_start()
		{
			if (output && input_left && input_right)
				start();
		}
	
		void start()
		{
			m_thread.start(&run, this);
		}

		static void* run(void* arg)
		{
			try
			{
				((JoiningAddProcess*)arg)->process();
			}
			catch (const dyplo::InterruptedException&)
			{
			}
			return NULL;
		}
};

int main(int argc, char** argv)
{
	try
	{
		dyplo::FixedMemoryQueue<std::string, dyplo::PthreadScheduler> q_input_strings(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_input_ints(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_to_left_adder(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_to_right_adder(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_from_left_adder(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_from_right_adder(2);
		dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_from_joining_adder(2);
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

		dyplo::ThreadedProcess<
			typeof(q_to_right_adder), typeof(q_from_right_adder),
			process_block_add_constant<int, 3, 1>
			> p_right_adder;
			
		JoiningAddProcess<typeof(q_from_left_adder),
			typeof(q_from_right_adder), typeof(q_from_joining_adder)
			> p_joining_adder;

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
		p_joining_adder.set_output(&q_from_joining_adder);
		p_joining_adder.set_input_left(&q_from_left_adder);
		p_joining_adder.set_input_right(&q_from_right_adder);
		p_left_adder.set_output(&q_from_left_adder);
		p_right_adder.set_output(&q_from_right_adder);
		p_left_adder.set_input(&q_to_left_adder);
		p_right_adder.set_input(&q_to_right_adder);
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
