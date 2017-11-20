#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <libvirt/libvirt.h>
#include <assert.h>

//ceiling of x/y
#define CEILING(x,y) (((x) + (y) - 1) / (y))

static const double MAXDIFF = 1.0;
static const double THRESHOLD = 20.0;
static const long SECONDTONANO = 1000000000;

/*
 * Self-defined data structures to store stats for vCPUs and pCPUs
 */
struct vcpuStats{
  int domain;
  int cpu;
  unsigned int number;
  unsigned long long time;
  double usage;
};

struct pcpuStats{
  unsigned long long time;
  double usage;
  double load;
};

struct cpuStats{
  struct vcpuStats* vcpu;
  struct pcpuStats* pcpu;
};

//set bit in cpumap
void setCpumap(unsigned char *cpumap, int maplen, int numCpu){
  int i;
  for(i=0; i<maplen; i++){
  	if(i == numCpu/8)
		cpumap[i] = 0x1 << (numCpu%8);	
	else
		cpumap[i] = 0x0;
  }
}

//calculate precentage usage (t1-t2) in time interval
double calculateUsage(unsigned long long t1, unsigned long long t2, int timeInterval){
	return ((double)(t1 - t2))/((double)(timeInterval*SECONDTONANO))*100;
}

//compare a and b in reverse order which results in a decreasing array
int reverseCmp(const void *a, const void *b)
{
    if( (*(struct vcpuStats*)a).usage - (*(struct vcpuStats*)b).usage < 0 )
        return 1;
    if( (*(struct vcpuStats*)a).usage - (*(struct vcpuStats*)b).usage > 0 )
        return -1;
    return 0;
}

//Main - entry point
int main(int argc, char **argv) {
  //check program argument [GOOD]
  if(argc != 2){
  	printf("Need program argument: time interval\n");
	exit(1);
  }
  int timeInterval = atoi(argv[1]);

  //declare variables [GOOD]
  int i,j,k;
  int maxCpus,maxVcpus,maxDomains,prevMaxDomains=0;
  int avgVcpus, maplen, above_threshold;
  double minUsage,maxUsage,minLoad;
  unsigned char *cpumap;
  virConnectPtr conn;
  virDomainPtr* domains = NULL;
  virVcpuInfoPtr info = NULL;
  struct cpuStats stats = {.vcpu = NULL, .pcpu = NULL} , prevStats = {.vcpu = NULL, .pcpu = NULL}; 
  struct vcpuStats *temp;
  //connect to Hypervisor [GOOD]
  if((conn = virConnectOpen("qemu:///system")) == NULL){
  	printf("Can't connect to Hypervisor\n");
  	exit(1);
  }
  assert(conn != NULL);

  printf("Connected\n");

  //collect Host Node info [GOOD]
  maxCpus = virNodeGetCPUMap(conn, NULL, NULL, 0);		//get number of CPUs present on the host node
  maplen = VIR_CPU_MAPLEN(maxCpus);				//number of bytes in CPUMAP
  cpumap = (unsigned char*)calloc(maplen,sizeof(unsigned char));			//allocate space for CPUMAP
  
  //while there are active running virtual machines within "qemu:///system"
  while((maxDomains = virConnectListAllDomains(conn,&domains,VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING)) > 0){
	//calculate total vcpus number [GOOD]
        maxVcpus = 0;
	for(i = 0; i<maxDomains; i++){
		maxVcpus += virDomainGetMaxVcpus(domains[i]);
	}

	//allocate memory space for stats structs [GOOD]
	stats.pcpu = (struct pcpuStats*)calloc(maxCpus,sizeof(struct pcpuStats));	//allocate pccpu stats array
	stats.vcpu = (struct vcpuStats*)calloc(maxVcpus,sizeof(struct vcpuStats));	//allocate vcpu stats array

	//i - domain count; j - max vcpu in each domain; k - vcpu count in each domain [GOOD]
	j = k = maxVcpus = 0;
	for(i = 0; i<maxDomains; i++){
		//collect vCPU stats 
		j = virDomainGetMaxVcpus(domains[i]);
		info = (virVcpuInfo*)calloc(j,sizeof(virVcpuInfo));
		if(virDomainGetVcpus(domains[i],info,j,NULL,0)<0){
			printf("Can not access domain info\n");			
			exit(1);		
		}
		//for vcpus in this domain
		for(k=0; k<j; k++){
			stats.vcpu[maxVcpus+k].domain = i;
			stats.vcpu[maxVcpus+k].number = info[k].number;
			stats.vcpu[maxVcpus+k].time = info[k].cpuTime;
			stats.vcpu[maxVcpus+k].cpu = info[k].cpu;		
		}
		free(info);		
		maxVcpus+=j;	
	}
	//there exits new VM or scheduler runs for the first time [GOOD]
	if(maxDomains != prevMaxDomains){
		//Evenly Pin pCPUs to vCPUs
		//i - vcpu count 
		avgVcpus = CEILING(maxVcpus,maxCpus);	//average number of vpus in each pcpu
		for(i = 0; i<maxVcpus; i++){
			if(i%avgVcpus==0){
				setCpumap(cpumap,maplen,i/avgVcpus);	//assign vcpus to next available pcpu					
			}
			virDomainPinVcpu(domains[i],stats.vcpu[i].number,cpumap,maplen); 			
		}
	}
	else{
		//calculate Vcpu usage [GOOD]
		//i - vcpu count
		for(i = 0; i<maxVcpus; i++){
			stats.vcpu[i].usage = calculateUsage(stats.vcpu[i].time,prevStats.vcpu[i].time,timeInterval);				
		}
	
		//calculate Pcpu usage by summing up usages of vcpus  [GOOD]
		//i - vcpu count;
		for(i=0; i<maxVcpus; i++){
			stats.pcpu[stats.vcpu[i].cpu].usage += stats.vcpu[i].usage;
		}

		//PIN ALGORITHM HERE
		/*
		 *   first check if we need to balance pcpu [GOOD]
		 *1) check if any pcpu usage is above threshold
		 *2) check the max difference in pcpus
		 */
		minUsage = 100; maxUsage = 0; above_threshold = 0;
		for(i=0; i<maxCpus; i++){
			above_threshold |= (stats.pcpu[i].usage > THRESHOLD);
			if(stats.pcpu[i].usage>maxUsage) maxUsage = stats.pcpu[i].usage;
			if(stats.pcpu[i].usage<minUsage) minUsage = stats.pcpu[i].usage;
		}
		//IF there exits pcpu usage > threshold and the difference between cpu is high enough
		if(above_threshold && maxUsage-minUsage > MAXDIFF){
			printf("Doing scheduling...\n");
			//copy vcpu to temp
			temp = (struct vcpuStats*)calloc(maxVcpus,sizeof(struct vcpuStats));
			memcpy(temp,stats.vcpu,maxVcpus);
			//sort vcpu in reverse order
			qsort(temp, maxVcpus, sizeof(struct vcpuStats), reverseCmp);
			//i - vcpu count j - pcpu count k - pcpu idx with min load
			for(i = 0; i<maxVcpus; i++){
				//find pcpu with min load
				k = 0;
				minLoad = 100;
				for(j = 0; j<maxCpus; j++){
					if(stats.pcpu[j].load < minLoad){
						minLoad = stats.pcpu[j].load;
						k = j;					
					}			
				}
				//assign vcpu i to k
				setCpumap(cpumap,maplen,k);
				virDomainPinVcpu(domains[temp[i].domain],temp[i].number,cpumap,maplen);
				stats.pcpu[k].load += temp[i].usage;	//update load for pcpu j
			}
			free(temp);
		}
		
	}
	//clean memory allocation
	free(prevStats.vcpu);
	free(prevStats.pcpu);
	for(i = 0; i<maxDomains;i++){
		virDomainFree(domains[i]);	
	}
	free(domains);
	//save stats in this time interval;
	prevStats.vcpu = stats.vcpu;
	prevStats.pcpu = stats.pcpu;
	prevMaxDomains = maxDomains;
	sleep(timeInterval);
  }
  free(stats.vcpu);
  free(stats.pcpu);
  free(cpumap);
  //Close connection to Hypervisor
  if(virConnectClose(conn) < 0){
  	printf("Can't close connection to Hypervisor\n");
  	exit(1);
  }
  return 0;
}




