// Functional test: exercise the mock libnvidia-ml.so.1 through the REAL
// github.com/NVIDIA/go-nvml binding (the same one GFD / device-plugin use), so
// we catch ABI/struct/soname mismatches a C unit test cannot. (TESTING.md §2.2)
//
// Run with LD_LIBRARY_PATH pointing at the directory holding the mock .so.
// Emits JSON on stdout; run.sh asserts the values against versions.mk.
package main

import (
	"encoding/json"
	"fmt"
	"os"

	"github.com/NVIDIA/go-nvml/pkg/nvml"
)

type device struct {
	Index       int    `json:"index"`
	Name        string `json:"name"`
	UUID        string `json:"uuid"`
	MemoryTotal uint64 `json:"memoryTotal"`
	CCMajor     int    `json:"ccMajor"`
	CCMinor     int    `json:"ccMinor"`
	Minor       int    `json:"minor"`
}

type result struct {
	DriverVersion string   `json:"driverVersion"`
	Count         int      `json:"count"`
	Devices       []device `json:"devices"`
}

func fail(msg string, ret nvml.Return) {
	fmt.Fprintf(os.Stderr, "FATAL: %s: %s\n", msg, nvml.ErrorString(ret))
	os.Exit(1)
}

func main() {
	if ret := nvml.Init(); ret != nvml.SUCCESS {
		fail("nvml.Init", ret)
	}
	defer nvml.Shutdown()

	res := result{}

	if dv, ret := nvml.SystemGetDriverVersion(); ret == nvml.SUCCESS {
		res.DriverVersion = dv
	} else {
		fail("SystemGetDriverVersion", ret)
	}

	count, ret := nvml.DeviceGetCount()
	if ret != nvml.SUCCESS {
		fail("DeviceGetCount", ret)
	}
	res.Count = count

	for i := 0; i < count; i++ {
		d, ret := nvml.DeviceGetHandleByIndex(i)
		if ret != nvml.SUCCESS {
			fail("DeviceGetHandleByIndex", ret)
		}
		name, ret := d.GetName()
		if ret != nvml.SUCCESS {
			fail("GetName", ret)
		}
		uuid, ret := d.GetUUID()
		if ret != nvml.SUCCESS {
			fail("GetUUID", ret)
		}
		mem, ret := d.GetMemoryInfo()
		if ret != nvml.SUCCESS {
			fail("GetMemoryInfo", ret)
		}
		ccMaj, ccMin, ret := d.GetCudaComputeCapability()
		if ret != nvml.SUCCESS {
			fail("GetCudaComputeCapability", ret)
		}
		minor, ret := d.GetMinorNumber()
		if ret != nvml.SUCCESS {
			fail("GetMinorNumber", ret)
		}
		res.Devices = append(res.Devices, device{
			Index:       i,
			Name:        name,
			UUID:        uuid,
			MemoryTotal: mem.Total,
			CCMajor:     ccMaj,
			CCMinor:     ccMin,
			Minor:       minor,
		})
	}

	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	if err := enc.Encode(res); err != nil {
		fmt.Fprintln(os.Stderr, "FATAL: encode:", err)
		os.Exit(1)
	}
}
