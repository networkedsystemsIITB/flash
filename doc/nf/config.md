UMEM Configuration Documentation
===============================

This document provides details on the UMEM configuration, including network functions (NF), threads, and other related settings.

UMEM Overview
-------------
The `umem` section defines memory objects used by the XDP program. Each UMEM object has associated parameters such as network interfaces, queues, XDP flags, bind flags, modes, and fragments.

UMEM Entries

For each UMEM entry, the configuration contains:
- **Interface Name**
- **Interface Queue Mask**
- **XDP Flags**
- **Bind Flags**
- **Mode**
- **Custom XSK**
- **Fragments Enabled**
- **Network Functions (NF)**

Each UMEM entry contains a list of Network Functions (NFs). The number of threads in each NF must be consistent across all NFs.

Example UMEM Structure:
Each UMEM object follows the structure below:

- **UMEM ID**: `<umem_id>`
  - **Interface Name**: `<ifname>`
  - **Interface Queue Mask**: `<ifqueue_mask>`
  - **XDP Flags**: `<xdp_flags>`
  - **Bind Flags**: `<bind_flags>`
  - **Mode**: `<mode>`
  - **Custom XSK**: `<custom_xsk>`
  - **Fragments Enabled**: `<frags_enabled>`

  **Network Functions**:
  - **NF ID**: `<nf_id>`
    - **Threads**: 
      - Thread ID: `<thread_id_1>`
      - Thread ID: `<thread_id_2>`
      - ...

  *Note*: The number of threads in each NF must be the same across all NFs for each UMEM entry.

### Example UMEM Entries:

**UMEM ID: 0**
- **Interface Name**: `ens865f0np0`
- **Interface Queue Mask**: `0xF`
- **XDP Flags**: `d`
- **Bind Flags**: `z`
- **Mode**: `b`
- **Custom XSK**: `false`
- **Fragments Enabled**: `false`

**Network Functions**:
- **NF ID: 0**
  - **Threads**: 
    - Thread ID: 0
    - Thread ID: 1
- **NF ID: 1**
  - **Threads**: 
    - Thread ID: 0
    - Thread ID: 1

**UMEM ID: 1**
- **Interface Name**: `ens865f0np0`
- **Interface Queue Mask**: `0xF0`
- **XDP Flags**: `d`
- **Bind Flags**: `z`
- **Mode**: `b`
- **Custom XSK**: `false`
- **Fragments Enabled**: `false`

**Network Functions**:
- **NF ID: 0**
  - **Threads**: 
    - Thread ID: 0
    - Thread ID: 1
- **NF ID: 1**
  - **Threads**:
    - Thread ID: 0
    - Thread ID: 1

Routing Configuration
---------------------
The `route` section specifies the routing paths between different NF IDs. Each NF is connected to others through specific routes.

### Route Mapping

For each NF, the routes to other NFs are specified as follows:

- **NF ID <nf_id>**: Routes to NF IDs `<nf_ids>`
  - Example: **NF ID 0** routes to NFs [1, 2]

*Note*: The routing section must be adapted to your specific use case to indicate which NFs each NF ID connects to.

### Thread Consistency Across NFs
-----------------------------
It is important to ensure that the number of threads in each network function (NF) is the same across all UMEM entries. This configuration ensures that all threads within each NF ID (across multiple UMEM entries) are consistent.

In the example configuration:
- **NF 0** and **NF 1** both have two threads (Thread ID 0 and Thread ID 1) across all UMEM entries, maintaining thread consistency.

