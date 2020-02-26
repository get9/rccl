/*
Copyright (c) 2019-2020 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include "nccl.h"
#include "channel.h"
#include "nvmlwrap.h"
#include "bootstrap.h"
#include "transport.h"
#include "group.h"
#include "net.h"
#include "graph.h"
#include "argcheck.h"
#include "cpuset.h"
#include <sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <hip/hip_runtime.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <iostream>
#include <cstring>
#include "model.h"
#include "utils.h"
#include "topo.h"

NodeModel *node_model;

char* getCmdOption(char ** begin, char ** end, const std::string & option) {
    char ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end)
    {
        return *itr;
    }
    return 0;
}

bool cmdOptionExists(char** begin, char** end, const std::string& option) {
    return std::find(begin, end, option) != end;
}

const char *model_descriptions[] = {
  "4 nodes with 8 GPUs PCIe 1 NIC",
  "4 nodes with 8 GPUs PCIe 2 NIC",
  "2 nodes VEGA20 4P1H",
  "4 nodes with 8 VEGA20 GPUs XGMI 4P2H 1 NIC",
  "single node gfx908 4P3L",
  "single node gfx908 8P6L",
  "single node gfx908 8P6L Alt. Connection",
  "single node 8 GPUs PCIe on Rome",
  "4 nodes 8 GPUs PCIe 2 NICs on Rome",
  "3 nodes 8 GPUs PCIe + 1 Rome 8 GPUs PCIe + 2 nodes gfx908 4P3L",
  NULL,
};

int main(int argc,char* argv[])
{
  struct ncclComm *comm;

  if (!cmdOptionExists(argv, argv + argc, "-m")) {
    printf("Usage: ./topo_expl -m model_id\n");
    printf("List of model_id:\n");
    for (int i = 0; model_descriptions[i] != NULL; i++)
      printf("  %d: %s\n", i, model_descriptions[i]);
    exit(0);
  }

  int model_id = 0;
  char *mi = getCmdOption(argv, argv + argc, "-m");
  if (mi)
    model_id = atol(mi);

  // CPU, GPU and NIC devices on Skylake
  CpuDevices skylake("Skylake", SKL_QPI_WIDTH, SKL_CPUPCI_WIDTH, SKL_PCI_WIDTH);
  GpuDevices vg20_pcie(8, busIds_8, gpuPciPaths_8, gpuPciNumaIds_8, conn_mat_pcie);
  GpuDevices vg20_4p1h(4, busIds_8, gpuPciPaths_8, gpuPciNumaIds_8, conn_mat_4p2h);
  GpuDevices vg20_4p2h(8, busIds_8, gpuPciPaths_8, gpuPciNumaIds_8, conn_mat_4p2h);
  GpuDevices gfx908_4p3l(4, busIds_8, gpuPciPaths_8, gpuPciNumaIds_8, conn_mat_8p6l);
  GpuDevices gfx908_8p6l(8, busIds_8, gpuPciPaths_8, gpuPciNumaIds_8, conn_mat_8p6l);
  GpuDevices gfx908_8p6l_1(8, busIds_8, gpuPciPaths_8, gpuPciNumaIds_8, conn_mat_8p6l_1);
  NetDevices nic_1(1, netPciPaths_1, netGuids_1, netPciNumaIds_1);
  NetDevices nic_1_1(1, netPciPaths_1_1, netGuids_1, netPciNumaIds_1);
  NetDevices nic_2(2, netPciPaths_2, netGuids_2, netPciNumaIds_2);

  // CPU, GPU and NIC devices on Rome
  CpuDevices rome("Rome", ROME_QPI_WIDTH, ROME_CPUPCI_WIDTH, ROME_PCI_WIDTH);
  GpuDevices vg20_pcie_rome(8, rome_busIds_8, rome_gpuPciPaths_8, rome_gpuPciNumaIds_8, conn_mat_rome);
  NetDevices nic_1_rome(1, rome_netPciPaths_1, rome_netGuids_1, rom_netPciNumaIds_1);
  NetDevices nic_2_rome(2, rome_netPciPaths_2, rome_netGuids_2, rom_netPciNumaIds_2);

  // 8 GPUs PCIe 1 NIC
  NodeModel model_8pcie_1nic(skylake, vg20_pcie, nic_1, "Skylake 8 GPUs PCIe");

  // 8 GPUs PCIe 2 NIC
  NodeModel model_8pcie_2nic(skylake, vg20_pcie, nic_2, "Skylake 8 GPUs PCIe 2 NIC");

  // VEGA20 4P1H, use VEGA20 4P2H model
  NodeModel model_vg20_4p1h_1nic(skylake, vg20_4p1h, nic_1, "Skylake VEGA20 4P1H");

  // VEGA20 GPUs XGMI 4P2H 1 NIC
  NodeModel model_vg20_4p2h_1nic(skylake, vg20_4p2h, nic_1_1, "Skylake VEGA20 4P2H");

  // gfx908 4P3L
  NodeModel model_gfx908_4p_1nic(skylake, gfx908_4p3l, nic_1, "Skylake gfx908 4P3L");

  // gfx908 8P6L
  NodeModel model_gfx908_8p_1nic(skylake, gfx908_8p6l, nic_1, "Skylake gfx908 8P6L");

  // gfx908 8P6L alternative connection
  NodeModel model_gfx908_8p_1nic_1(skylake, gfx908_8p6l_1, nic_1, "Skylake gfx908 8P6L Alt. Connection");

  // 8 GPUs PCIe on Rome
  NodeModel model_8pcie_1nic_rome(rome, vg20_pcie_rome, nic_1_rome, "Rome 8 GPUs PCIe");

  // 8 GPUs PCIe 2 NICs on Rome
  NodeModel model_8pcie_2nic_rome(rome, vg20_pcie_rome, nic_2_rome, "Rome 8 GPUs PCIe 2 NICs");

  NetworkModel network;

  switch(model_id) {
    case 0:
      for (int i = 0; i < 4; i ++) network.AddNode(model_8pcie_1nic);
      break;
    case 1:
      for (int i = 0; i < 4; i ++) network.AddNode(model_8pcie_2nic);
      break;
    case 2:
      for (int i = 0; i < 2; i ++) network.AddNode(model_vg20_4p1h_1nic);
      break;
    case 3:
      for (int i = 0; i < 4; i ++) network.AddNode(model_vg20_4p2h_1nic);
      break;
    case 4:
      network.AddNode(model_gfx908_4p_1nic);
      break;
    case 5:
      network.AddNode(model_gfx908_8p_1nic);
      break;
    case 6:
      network.AddNode(model_gfx908_8p_1nic_1);
      break;
    case 7:
      network.AddNode(model_8pcie_1nic_rome);
      break;
    case 8:
      for (int i = 0; i < 4; i ++) network.AddNode(model_8pcie_2nic_rome);
      break;
    case 9:
      for (int i = 0; i < 3; i ++) network.AddNode(model_8pcie_1nic);
      network.AddNode(model_8pcie_1nic_rome);
      for (int i = 0; i < 2; i ++) network.AddNode(model_gfx908_4p_1nic);
      break;
    default:
      printf("Invalid model_id %d\n", model_id);
      exit(0);
  }

  printf("Generating topology using %d: %s\n", model_id, model_descriptions[model_id]);

  int nranks = network.GetNRanks();
  int nnodes = network.GetNNodes();

  printf("nnodes = %d, nranks = %d\n", nnodes, nranks);
  for (int i = 0; i < nranks; i++) {
    node_model = network.GetNode(i);
    assert(node_model!=0);
    printf("Rank %d: node %d (%s) GPU busId %lx\n", i, node_model->nodeId,
      node_model->description, node_model->getGpuBusId(node_model->rankToCudaDev(i)));
  }

  NCCLCHECK(ncclCalloc(&comm, nranks));

  struct allGather1Data_t *allGather1Data;
  NCCLCHECK(ncclCalloc(&allGather1Data, nranks));

  struct allGather3Data_t *allGather3Data;
  NCCLCHECK(ncclCalloc(&allGather3Data, nranks));

  for (int i = 0; i < nranks; i++) {
    comm[i].rank = i;
    comm[i].nRanks = nranks;
    node_model = network.GetNode(i);
    assert(node_model!=0);
    bootstrapAllGather(&comm[i], allGather1Data);
  }

  struct ncclTopoGraph treeGraph, ringGraph;

  for (int i = 0; i < nranks; i++) {
    node_model = network.GetNode(i);
    assert(node_model!=0);
    initTransportsRank_1(&comm[i], allGather1Data, allGather3Data, treeGraph, ringGraph);
  }

  for (int i = 0; i < nranks; i++) {
    node_model = network.GetNode(i);
    assert(node_model!=0);
    initTransportsRank_3(&comm[i], allGather3Data, treeGraph, ringGraph);
  }

  free(allGather3Data);
  free(allGather1Data);

  free(comm);
  printf("Done generating topology using %d: %s\n", model_id, model_descriptions[model_id]);

  return 0;
}