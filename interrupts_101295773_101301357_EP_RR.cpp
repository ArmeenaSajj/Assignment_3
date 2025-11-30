/**
 * @file interrupts.cpp
 * @author Sasisekhar Govind
 * @brief template main.cpp file for Assignment 3 Part 1 of SYSC4001
 * @author Armeena Sajjad
 * @author Salma Khai
 */

#include<interrupts_101295773_101301357.hpp>
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

// helper used to look at priorities in the ready queue
static int get_priority(const PCB &p,
                        const std::unordered_map<int,int> &priority_map) {
    auto it = priority_map.find(p.PID);
    if (it != priority_map.end()) {
        return it->second;
    }
    return p.PID;
}

// choose the next process with best external priority
static int select_best_index(const std::vector<PCB> &ready_queue,
                             const std::unordered_map<int,int> &priority_map) {
    if (ready_queue.empty()) return -1;
    int best_index = 0;
    int best_prio = get_priority(ready_queue[0], priority_map);
    for (std::size_t i = 1; i < ready_queue.size(); ++i) {
        int pr = get_priority(ready_queue[i], priority_map);
        if (pr < best_prio) {
            best_prio = pr;
            best_index = static_cast<int>(i);
        }
    }
    return best_index;
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

    // IO timing for each PID
    std::unordered_map<int,unsigned int> cpu_to_next_io;
    std::unordered_map<int,unsigned int> io_time_remaining;

    // external priorities per PID
    std::unordered_map<int,int> priority_map;

    for (auto &p : list_processes) {
        cpu_to_next_io[p.PID] = (p.io_freq > 0 ? p.io_freq : 0);
        io_time_remaining[p.PID] = 0;
        // external priority based on PID (can be changed if needed)
        priority_map[p.PID] = p.PID;
    }

    const unsigned int TIME_QUANTUM = 100;
    unsigned int quantum_left = 0;

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
            if(process.arrival_time <= current_time && process.state == NOT_ASSIGNED) {//check if the AT = current time
                //if so, assign memory and put the process into the ready queue
                if (assign_memory(process)) {

                    process.state = READY;  //Set the process state to READY
                    ready_queue.push_back(process); //Add the process to the ready queue
                    job_list.push_back(process); //Add it to the list of processes

                    execution_status += print_exec_status(current_time, process.PID, NEW, READY);

                    // memory status when a new process is admitted
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
            }
        }

        ///////////////////////MANAGE WAIT QUEUE/////////////////////////
        //This mainly involves keeping track of how long a process must remain in the ready queue

        for (auto it = wait_queue.begin(); it != wait_queue.end(); ) {
            int pid = it->PID;

            if (io_time_remaining[pid] > 0) {
                io_time_remaining[pid]--;
            }

            if (io_time_remaining[pid] == 0) {
                states old_state = WAITING;
                it->state = READY;
                ready_queue.push_back(*it);

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
        // preemption when a higher-priority process is ready
        if (running.state == RUNNING && !ready_queue.empty()) {
            int cur_prio = get_priority(running, priority_map);
            int best_idx = select_best_index(ready_queue, priority_map);
            if (best_idx >= 0) {
                int best_prio = get_priority(ready_queue[best_idx], priority_map);
                if (best_prio < cur_prio) {
                    states old_state = RUNNING;
                    running.state = READY;

                    sync_queue(job_list, running);
                    execution_status += print_exec_status(current_time, running.PID, old_state, READY);

                    // put preempted process back into ready queue
                    ready_queue.push_back(running);
                    idle_CPU(running);
                    quantum_left = 0;
                }
            }
        }

        // choose next process when CPU is idle (highest external priority, RR inside level)
        if (running.state == NOT_ASSIGNED && !ready_queue.empty()) {
            int idx = select_best_index(ready_queue, priority_map);
            if (idx < 0) idx = 0;

            PCB next = ready_queue[static_cast<std::size_t>(idx)];
            ready_queue.erase(ready_queue.begin() + idx);

            states old_state = READY;
            next.state = RUNNING;
            if (next.start_time == -1) {
                next.start_time = current_time;
            }
            running = next;

            sync_queue(job_list, running);
            execution_status += print_exec_status(current_time, running.PID, old_state, RUNNING);

            quantum_left = TIME_QUANTUM;
        }
        /////////////////////////////////////////////////////////////////

        if (running.state == RUNNING) {
            int pid = running.PID;

            if (running.remaining_time > 0) {
                running.remaining_time--;
            }

            if (running.io_freq > 0 && cpu_to_next_io[pid] > 0) {
                cpu_to_next_io[pid]--;
            }

            if (quantum_left > 0) {
                quantum_left--;
            }

            sync_queue(job_list, running);

            // finished work
            if (running.remaining_time == 0) {
                states old_state = RUNNING;
                terminate_process(running, job_list);
                execution_status += print_exec_status(current_time, pid, old_state, TERMINATED);
                idle_CPU(running);
                quantum_left = 0;
            }
            // IO request
            else if (running.io_freq > 0 && cpu_to_next_io[pid] == 0) {
                states old_state = RUNNING;
                running.state = WAITING;

                io_time_remaining[pid] = running.io_duration;

                sync_queue(job_list, running);
                execution_status += print_exec_status(current_time, pid, old_state, WAITING);

                wait_queue.push_back(running);
                idle_CPU(running);
                quantum_left = 0;
            }
            // quantum expired: RR inside same priority level
            else if (quantum_left == 0) {
                states old_state = RUNNING;
                running.state = READY;

                sync_queue(job_list, running);
                execution_status += print_exec_status(current_time, pid, old_state, READY);

                ready_queue.push_back(running);
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

