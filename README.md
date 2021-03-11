﻿# Memory-mangment-in-XV6


This version of XV6 includes a page handling system and Copy on Write mechanism when creating a new process, also the page handling can be modified by changing the algorithm of the page swap.

The page handling is maintained by the page table of every process, in the project we ensure that every process can handle at most 16 physical pages, and when it's exceeded, we activate the page swap current algorithm. This assignment also includes handling page fault if we tried to write to a read-only page or a page swap is required (this can be seen in trap.c)

COW mechanism is invoked whenever a fork function is called (new process created), with this mechanism we don't need to copy all the values of the table page of the "father" process, instead, we gave the created process a direct reference to the "father" page table.
