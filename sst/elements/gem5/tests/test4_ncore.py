import sst
import sys
import os

lat="1 ns"
buslat="2 ns"
clockRate = "1GHz"
cores = 4


def getenv(name):
    res = ""
    try:
        res = os.environ[name]
    except KeyError:
        pass
    return res

baseCacheParams = ({
    "debug" : 0,
    "debug_level" : 7,
    "coherence_protocol" : "MSI",
    "replacement_policy" : "LRU",
    "cache_line_size" : 64,
    "mshr_num_entries" : 4096,
    "cache_frequency" : clockRate,
    "statistics" : 1
    })

l1CacheParams = ({
    "L1" : 1,
    "cache_size" : "64 KB",
    "associativity" : 4,
    "access_latency_cycles" : 2,
    "mshr_latency_cycles" : 1,
    "low_network_links" : 1
    })

l2CacheParams = ({
    "L1" : 0,
    "cache_size" : "256 KB",
    "associativity" : 8,
    "access_latency_cycles" : 8,
    "mshr_latency_cycles" : 2,
    "high_network_links" : 1,
    "low_network_links" : 1
    })


connectors = "system.mem_ctrls.connector"
for x in range(cores):
    connectors = connectors + " system.cpu%d.icache"%x
    connectors = connectors + " system.cpu%d.dcache"%x
    connectors = connectors + " system.cpu%d.itb_walker_cache"%x
    connectors = connectors + " system.cpu%d.dtb_walker_cache"%x

GEM5 = sst.Component("system", "gem5.Gem5")
GEM5.addParams({
    "comp_debug" : getenv("GEM5_DEBUG"),
    "gem5DebugFlags" : getenv("M5_DEBUG"),
    "frequency" : clockRate,
    #"cmd" : "test_fs.py --disk-image=linux-x86.img --kernel=x86_64-vmlinux-2.6.22.9.smp --script=shutdown_script --mem-size=512MB --cpu-type=timing --external-caches --mem-type=InitializerMemory",
    "cmd" : "test_fs.py --disk-image=linux-x86.img --kernel=x86_64-vmlinux-2.6.22.9.smp --mem-size=512MB --cpu-type=timing --external-caches --mem-type=InitializerMemory --num-cpus=%d"%cores,
    "connectors" : connectors
    #"connectors" : "system.cpu.icache system.cpu.dcache system.cpu.itb_walker_cache system.cpu.dtb_walker_cache system.mem_ctrls.connector"
        # system.cpu0.icache, system.cpu0.dcache
    })

def buildL1(name, m5, connector):
    cache = sst.Component(name, "memHierarchy.Cache")
    cache.addParams(baseCacheParams)
    cache.addParams(l1CacheParams)
    link = sst.Link("cpu_%s_link"%name)
    link.connect((m5, connector, lat), (cache, "high_network_0", lat))

    return cache


bus = sst.Component("membus", "memHierarchy.Bus")
bus.addParams({
    "bus_frequency": "2GHz",
    "debug" : getenv("DEBUG")
    })
SysBusConn = buildL1("Gem5SysBus", GEM5, "system.mem_ctrls.connector")
link = sst.Link("sysbus_bus_link")
link.connect((SysBusConn, "low_network_0", buslat), (bus, "high_network_0", buslat))

def buildCore(core, bus, busPort):
    """Returns the number of the next busPort"""
    sst.pushNamePrefix("c%d"%core)
    l1iCache = buildL1("l1iCache", GEM5, "system.cpu%d.icache"%core)
    link = sst.Link("l1iCache_bus_link")
    link.connect((l1iCache, "low_network_0", buslat), (bus, "high_network_%d"%(busPort), buslat))
    busPort = busPort + 1
    l1dCache = buildL1("l1dCache", GEM5, "system.cpu%d.dcache"%core)
    link = sst.Link("l1dCache_bus_link")
    link.connect((l1dCache, "low_network_0", buslat), (bus, "high_network_%d"%(busPort), buslat))
    busPort = busPort + 1
    itlbCache = buildL1("itlbCache", GEM5, "system.cpu%d.itb_walker_cache"%core)
    link = sst.Link("itlbCache_bus_link")
    link.connect((itlbCache, "low_network_0", buslat), (bus, "high_network_%d"%(busPort), buslat))
    busPort = busPort + 1
    dtlbCache = buildL1("dtlbCache", GEM5, "system.cpu%d.dtb_walker_cache"%core)
    link = sst.Link("dtlbCache_bus_link")
    link.connect((dtlbCache, "low_network_0", buslat), (bus, "high_network_%d"%(busPort), buslat))
    busPort = busPort + 1

    sst.popNamePrefix()
    return busPort

busPort = 1
for x in range(cores):
    busPort = buildCore(x, bus, busPort)

l2cache = sst.Component("l2cache", "memHierarchy.Cache")
l2cache.addParams(baseCacheParams)
l2cache.addParams(l2CacheParams)

link = sst.Link("l2cache_bus_link")
link.connect((l2cache, "high_network_0", buslat), (bus, "low_network_0", buslat))

memory = sst.Component("memory", "memHierarchy.MemController")
memory.addParams({
    "request_width" : 64,
    "coherence_protocol" : "MSI",
    "access_time" : "25 ns",
    "mem_size" : 512,
    "clock" : "2GHz",
    "debug" : getenv("DEBUG")
    })

link = sst.Link("l2cache_mem_link")
link.connect((l2cache, "low_network_0", lat), (memory, "direct_link", lat))

