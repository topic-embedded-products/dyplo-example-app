/*
 * softwareprocesses.hpp
 *
 * Dyplo example application.
 *
 * (C) Copyright 2013,2014 Topic Embedded Products B.V. <Mike Looijmans> (http://www.topic.nl).
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
 * along with <product name>.  If not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA or see <http://www.gnu.org/licenses/>.
 *
 * You can contact Topic by electronic mail via info@topic.nl or via
 * paper mail at the following address: Postbus 440, 5680 AK Best, The Netherlands.
 */
#include "dyplo/threadedprocess.hpp"
#include "dyplo/cooperativescheduler.hpp"
#include "dyplo/cooperativeprocess.hpp"
#include "dyplo/thread.hpp"

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
