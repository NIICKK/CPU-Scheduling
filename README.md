# CPU-Scheduling
vCPU scheduler dynamically manage the resources assigned to each guest machine
Compile

Just use command "make" in terminal. Make sure libvirt/libvirt.h is installed properly in include folder.

Usage

./vcpu_scheduler [time_interval] 

eg ./vcpu_scheduler 12

Algorithm

Initially, the scheduler allocates vcpus evenly to pcpus so that numbers of vcpus allocated to each pcpu are balanced.
Later on, it uses a simple greedy algorithm:

sort vcpus usage from large to small
for each vcpu in this sorted order:
	find the pcpu with min load 
	allocate current vcpu to this pcpu


Also, the algorithm checks certain conditions for scheduling. It only schedules if:
1) at least one pcpu has a load > threshold
2) the max difference between max load pcpu and min load > maxdiff
In my program, threshold is set to 20.0 and maxdiff is set to 1.0 in default. 


