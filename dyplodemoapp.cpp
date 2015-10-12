/*
 * dyplodemoapp.cpp
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

/*  This file is compiled twice, once with and once without HAVE_HARDWARE
 *  defined. The version without HAVE_HARDWARE can run on any posix
 *  system (e.g. a PC), the hardware enabled version needs the Dyplo
 *  (FPGA) logic and driver present.
 *

 *  This application works as follows:

           ------------------
           | Keyboard input |
           | (type a number)|
           ------------------
                   |
                   |
         ------------------------
         | Tee node (SW).       |
         | Replicates the input |
         | to two outputs.      |
         ------------------------
                /           \
               /             \
 ------------------------     \
 | Adder Node (SW / HW).|      \
 | The number to add    |       \
 | can be configured.   |        \
 ------------------------         \
                    \              \
                     \   ------------------------------
                      \  | Joining Adder Node (SW/HW).|
                       \ | Accumulates two inputs,    |
                         | left and right             |
                         ------------------------------
                              |
                              |
                     -----------------------
                     | Display Node (SW)   |
                     | Displays the result |
                     -----------------------

*/

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
  #include "dyplo/hardware.hpp"
  #include "dyplo/filequeue.hpp"
#endif

template <class T, int raise, int blocksize> void process_block_add_constant(T* dest, T* src)
{
  for (int i = 0; i < blocksize; ++i)
    *dest++ = (*src++) + raise;
}

int string_to_int(const std::string& src)
{
  std::string src_digits = "";

  /* Loop to remove non-digit characters */
  for ( int i = 0; i < src.size(); ++i )
  {
    if (isdigit(src.at(i)))
    {
      src_digits += src.at(i);
    }
  }

  int result = atoi(src_digits.c_str());
  return result;
}

void display_int(int* src)
{
  std::cout << *src << std::endl;
}

int main(int argc, char** argv)
{
  try
  {
#ifdef HAVE_HARDWARE
    // Create objects for hardware control
    dyplo::HardwareContext hardware;
    dyplo::HardwareControl hwControl(hardware);
    dyplo::FilePollScheduler hardwareScheduler;

    // All bitstreams are partial streams.
    hardware.setProgramMode(true);

    std::string libraryName = "hdl_node_examples";
       
    // Search for "adder"  task on node index 1
    std::string taskName = "adder";
    std::string fullTaskName = libraryName + "__" + taskName;
    std::string filename = hardware.findPartition(fullTaskName.c_str(), 1);
    dyplo::HardwareConfig adderCfg(hardware, 1);
    // Program "adder" task on FPGA
    adderCfg.disableNode();
    hardware.program(filename.c_str());
    adderCfg.enableNode();
   
    // Search for "joining_adder" task on node index 2
    taskName = "joining_adder";
    fullTaskName = libraryName + "__" + taskName;
    filename = hardware.findPartition(fullTaskName.c_str(), 2);
    // Program "joining_adder" task on FPGA
    dyplo::HardwareConfig joiningAdderCfg(hardware, 2);
    joiningAdderCfg.disableNode();
    hardware.program(filename.c_str());
    joiningAdderCfg.enableNode();
   
    hardware.setProgramMode(false);
#endif

/* --- STEP 1 - CREATE QUEUES ---
   Create the queues first, because they need to exist before
   the processes are started, and at least for as long as the
   processes run.
*/
    dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_input(2);
#ifdef HAVE_HARDWARE
    dyplo::HardwareFifo f_adder(hardware.openFifo(0, O_WRONLY));
    dyplo::FileOutputQueue<int, true> q_adder(hardwareScheduler, f_adder, 16);
    dyplo::HardwareFifo f_joining_adder_right(hardware.openFifo(1, O_WRONLY));
    dyplo::FileOutputQueue<int, true> q_joining_adder_right(hardwareScheduler, f_joining_adder_right, 16);
    dyplo::HardwareFifo f_output(hardware.openFifo(0, O_RDONLY));
    dyplo::FileInputQueue<int> q_output(hardwareScheduler, f_output, 16);
#else
    dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_adder(2);
    dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_joining_adder_left(2);
    dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_joining_adder_right(2);
    dyplo::FixedMemoryQueue<int, dyplo::PthreadScheduler> q_output(2);
#endif

/* --- STEP 2 - CREATE PROCESSES --- */    
    TeeProcess<typeof(q_input), typeof(q_adder), typeof(q_joining_adder_right)> p_tee;
    // This number will be added by the 'adder function':
    const int number_to_add = 8;
#ifdef HAVE_HARDWARE
    // Hardware processes don't need CPU resources, but they often need configuration.
    // The simplest method is to just write to the configuration "file", the data will be sent
    // via the AXI bus to the offsets corresponding to the file position.
    adderCfg.write(&number_to_add, sizeof(number_to_add));
#else
    dyplo::ThreadedProcess<typeof(q_adder), typeof(q_joining_adder_left), process_block_add_constant<int, number_to_add, 1> > p_adder;
    JoiningAddProcess<typeof(q_joining_adder_left), typeof(q_joining_adder_right), typeof(q_output)> p_joining_adder;
#endif
    ThreadedProcessSink<typeof(q_output), display_int, 1> p_display_int;

/*  --- STEP 3 - CONNECT PROCESSES ---
    Connect the processes and queues from output to input.
*/
    p_tee.set_input(&q_input);
    p_tee.set_output_left(&q_adder);
    p_tee.set_output_right(&q_joining_adder_right);
#ifdef HAVE_HARDWARE
    // CPU node Fifo 0 to node 1 fifo 0
    f_adder.addRouteTo(1);
    // Node 1 fifo 0 to node 2 fifo 0
    hwControl.routeAddSingle(1, 0, 2, 0);
    // CPU node Fifo 1 to node 2 fifo 1
    f_joining_adder_right.addRouteTo(2 | (1 << 8));
    // Node 2 to CPU node fifo 0
    f_output.addRouteFrom(2);
#else
    p_adder.set_input(&q_adder);
    p_adder.set_output(&q_joining_adder_left);

    p_joining_adder.set_input_left(&q_joining_adder_left);
    p_joining_adder.set_input_right(&q_joining_adder_right);
    p_joining_adder.set_output(&q_output);
#endif
    p_display_int.set_input(&q_output);

    // Loop reading the input until end of file. Note that output
    // is not handled correctly, the program will simply 'abort'
    // and data present in the processing pipeline may be lost.
    for(;;)
    {
      std::string line;
      std::cin >> line;
      if (std::cin.eof())
        break;
      q_input.push_one(string_to_int(line));
    }
  }
  catch (const std::exception& ex)
  {
    std::cerr << "ERROR:\n" << ex.what() << std::endl;
    return 1;
  }
  return 0;
}
