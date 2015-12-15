### What is OFFSET ?
OFFSET is a flash filesystem specially designed for embedded devices with small size of NAND/NOR flash (and of course tiny amount of RAM). It's suitable for situations that you need to store small pieces of data beside your existing firmware inside your chipset (or even in an external flash chip) and you need to access, modify or delete this data frequently, easily and rapidly.<br/>
OFFSET is an object-based filesystem, it means that each file is supposed as an independent object with a unique handle and some special attributes that can be added, edited or deleted independently. You can search for objects by their unique handle or iterate on all existing objects in filesystem. You can also iterate on each object's attributes or search for an special attribute by its type.<br/>
OFFSET supports 4 levels of caching in order to maximize access and search speed that makes it a really fast filesystem. OFFSET also supports data wear leveling to prolong device life, in addition to automatic garbage collection, power failure recovery, authorized data access and strong data encryption. It's compatible with any standard C++ compiler, easy to config, easy to use and it eventually makes the life more beautiful!<br/>

### Features
* Supports NAND/NOR flash types
* Up to 4 levels of caching in order to minimize data access time
* Data wear leveling that prolongs device life
* Automatic garbage collection
* Automatic power failure recovery
* Authorized data access
* Strong data encryption using any optional block cipher algorithm
* Compatible with any standard C++ compiler
* Easy to config and use
* Tiny footprint

### Requirements
* Any MCU with a standard C++ compiler (ARM, AVR, MSP, etc.)
* Any flash chipset with at least two continuous independently formattable sectors
* A geek guy, a cup of coffee and a big piece of creamy cake!

### How to start ?
See [Getting Started](https://github.com/khodarahmi/OFFSET/wiki/Getting-Started) page.
