/**
 * @file interrupts.cpp
 * @author Sasisekhar Govind
 * @brief template main.cpp file for Assignment 3 Part 1 of SYSC4001
 * @author Armeena Sajjad
 * @author Salma Khai
 * 
 */

#include<interrupts_student1_student2.hpp>
#include<unordered_map>

void FCFS(std::vector<PCB> &ready_queue) {
    std::sort( 
                ready_queue.begin(),
                ready_queue.end(),
                []( const PCB &first, const PCB &second ){
                    return (first.arrival_time > second.arrival_time); 
                } 
            );
}

// helper for external priority scheduling (lower value = higher priority)
static void ExternalPriority(std::vector<PCB> &ready_queue,
                             const std::unordered_map<int,int> &priority_map) {
    std::sort(
        ready_queue.begin(),
        ready_queue.end(),
        [&](const PCB &a, const PCB &b){
            int pa = 0;
            int pb = 0;
            auto ita = priority_map.find(a.PID);
            auto itb = priority_map.find(b.PID);
            if (ita != priority_map.end()) pa = ita->second;
            if (itb != priority_map.end()) pb = itb->second;

            if (pa == pb) {
                // tie-break by arrival time so older processes go first
                return a.arrival_time > b.arrival_time;
            }
            // lower priority value should come later in vector so back() is best
            return pa > pb;
        }
    );
}

std::tuple<std::string, std::string /* add std::string for bonus mark */ > run_simulation(std::vector<PCB> list_processes) {

    std::vector<PCB> ready_queue;   //The ready queue of processes
    std::vector<PCB> wait_queue;    //The wait queue of processes
    std::vector<PCB> job_list;      //A list to keep track of all the processes. This is similar
                                    //to the "Process, Arrival time, Burst time" table that you
                                    //see in questions. You don't need to use it, I put it here
                                    //to make the code easier :).

    unsigned int current_time = 0;
    PCB running;

    //Initialize an empty running process
    idle_CPU(running);

    std::string execution_status;
    std::string memory_status;

    // track IO timing for each PID
    std::unordered_map<int,unsigned int> cpu_to_next_io;
    std::unordered_map<int,unsigned int> io_time_remaining;

    // external priority value for each PID
    std::unordered_map<int,int> priority_map;

    for (auto &p : list_processes) {
        cpu_to_next_io[p.PID] = (p.io_freq > 0 ? p.io_freq : 0);
        io_time_remaining[p.PID] = 0;
        // simple external priority: smaller PID => higher priority
        priority_map[p.PID] = p.PID;
    }

    // pre-compute total physical memory from partitions
    unsigned int total_memory = 0;
    for (const auto &part : memory_paritions) {
        total_memory += part.size;
    }

    //make the output table (the header row)
    execution_status = print_exec_header();

    //Loop while till there are no ready or waiting processes.
    //This is the main reason I have job_list, you don't have to use it.
    while(!all_process_terminated(job_list) || job_list.empty()) {

        //Inside this loop, there are three things you must do:
        // 1) Populate the ready queue with processes as they arrive
        // 2) Manage the wait queue
        // 3) Schedule processes from the ready queue

        //Population of ready queue is given to you as an example.
        //Go through the list of proceeses
        for(auto &process : list_processes) {
            // allow late admission when memory becomes available
            if(process.arrival_time <= current_time && process.state == NOT_ASSIGNED) {//check if the AT = current time
                //if so, assign memory and put the process into the ready queue
                if (assign_memory(process)) {

                    process.state = READY;  //Set the process state to READY
                    ready_queue.push_back(process); //Add the process to the ready queue
                    job_list.push_back(process); //Add it to the list of processes

                    execution_status += print_exec_status(current_time, process.PID, NEW, READY);

                    // record memory usage when a process is admitted
                    unsigned int used_by_processes = 0;
                    for (const auto &p : job_list) {
                        if (p.state != TERMINATED && p.partition_number != -1) {
                            used_by_processes += p.size;
                        }
                    }

                    unsigned int total_free_memory = total_memory - used_by_processes;

                    unsigned int usable_memory = 0;
                    std::string used_parts, free_parts;
                    for (const auto &part : memory_paritions) {
                        if (part.occupied == -1) {
                            usable_memory += part.size;
                            if (!free_parts.empty()) free_parts += ",";
                            free_parts += std::to_string(part.partition_number);
                        } else {
                            if (!used_parts.empty()) used_parts += ",";
                            used_parts += std::to_string(part.partition_number);
                        }
                    }

                    memory_status += "Time " + std::to_string(current_time)
                                   + " PID " + std::to_string(process.PID)
                                   + " started in partition " + std::to_string(process.partition_number) + "\n";
                    memory_status += "  Total memory used by processes: " + std::to_string(used_by_processes) + " Mb\n";
                    memory_status += "  Total free memory (including internal fragments): " + std::to_string(total_free_memory) + " Mb\n";
                    memory_status += "  Usable free memory in free partitions: " + std::to_string(usable_memory) + " Mb\n";
                    memory_status += "  Used partitions: " + (used_parts.empty() ? std::string("none") : used_parts) + "\n";
                    memory_status += "  Free partitions: " + (free_parts.empty() ? std::string("none") : free_parts) + "\n\n";
                }
                // if memory not available, process stays NOT_ASSIGNED and will be retried
            }
        }

        ///////////////////////MANAGE WAIT QUEUE/////////////////////////
        //This mainly involves keeping track of how long a process must remain in the ready queue

        // handle IO completion and move processes back to READY
        for (auto it = wait_queue.begin(); it != wait_queue.end(); ) {
            int pid = it->PID;

            if (io_time_remaining[pid] > 0) {
                io_time_remaining[pid]--;
            }

            if (io_time_remaining[pid] == 0) {
                states old_state = WAITING;
                it->state = READY;
                ready_queue.push_back(*it);

                // reset distance to next IO burst for this process
                cpu_to_next_io[pid] = it->io_freq;

                sync_queue(job_list, *it);
                execution_status += print_exec_status(current_time, it->PID, old_state, READY);

                it = wait_queue.erase(it);
            } else {
                ++it;
            }
        }

        /////////////////////////////////////////////////////////////////

        //////////////////////////SCHEDULER//////////////////////////////
        // non-preemptive external priorities: schedule only when CPU is idle
        if (running.state == NOT_ASSIGNED && !ready_queue.empty()) {
            ExternalPriority(ready_queue, priority_map); //example of FCFS is shown here

            PCB next = ready_queue.back();
            ready_queue.pop_back();

            states old_state = READY;
            next.state = RUNNING;
            if (next.start_time == -1) {
                next.start_time = current_time;
            }
            running = next;

            sync_queue(job_list, running);
            execution_status += print_exec_status(current_time, running.PID, old_state, RUNNING);
        }
        /////////////////////////////////////////////////////////////////

        // execute one millisecond of CPU time for the running process
        if (running.state == RUNNING) {
            int pid = running.PID;

            if (running.remaining_time > 0) {
                running.remaining_time--;
            }

            // update CPU time until next IO operation
            if (running.io_freq > 0 && cpu_to_next_io[pid] > 0) {
                cpu_to_next_io[pid]--;
            }

            sync_queue(job_list, running);

            // finished all CPU
            if (running.remaining_time == 0) {
                states old_state = RUNNING;
                terminate_process(running, job_list);
                execution_status += print_exec_status(current_time, pid, old_state, TERMINATED);
                idle_CPU(running);
            }
            // request IO
            else if (running.io_freq > 0 && cpu_to_next_io[pid] == 0) {
                states old_state = RUNNING;
                running.state = WAITING;

                io_time_remaining[pid] = running.io_duration;

                sync_queue(job_list, running);
                execution_status += print_exec_status(current_time, pid, old_state, WAITING);

                wait_queue.push_back(running);
                idle_CPU(running);
            }
        }

        current_time++;
    }
    
    //Close the output table
    execution_status += print_exec_footer();

    return std::make_tuple(execution_status, memory_status);
}


int main(int argc, char** argv) {

    //Get the input file from the user
    if(argc != 2) {
        std::cout << "ERROR!\nExpected 1 argument, received " << argc - 1 << std::endl;
        std::cout << "To run the program, do: ./interrutps <your_input_file.txt>" << std::endl;
        return -1;
    }

    //Open the input file
    auto file_name = argv[1];
    std::ifstream input_file;
    input_file.open(file_name);

    //Ensure that the file actually opens
    if (!input_file.is_open()) {
        std::cerr << "Error: Unable to open file: " << file_name << std::endl;
        return -1;
    }

    //Parse the entire input file and populate a vector of PCBs.
    //To do so, the add_process() helper function is used (see include file).
    std::string line;
    std::vector<PCB> list_process;
    while(std::getline(input_file, line)) {
        auto input_tokens = split_delim(line, ", ");
        auto new_process = add_process(input_tokens);
        list_process.push_back(new_process);
    }
    input_file.close();

    //With the list of processes, run the simulation
    auto [exec, mem_status] = run_simulation(list_process);

    write_output(exec, "execution.txt");
    write_output(mem_status, "memory_status.txt");

    return 0;
}
