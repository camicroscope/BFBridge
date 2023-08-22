# Two ways to handle JVM threads:
# 1) Reference counting as with this file.
# Eager thread-detaching approach.
# Works best if most of the computational
# work will be done outside the JVM.
# Allows constructing multiple BFBridgeThread objects.
# 2) Thread local method
# Lazy thread-detaching approach
# works best if the path taken would free the BFBridgeThread
# object but need later again in the same thread
# If neither scenario, then both approaches are fine.
# Desired: 1) Reuse of BFThread object
# 2) keeping alive if it will be reconstructed
# (or at least one instance is alive)


# Global (Variable) Thread Manager

import threading

thread_id_to_ref_count = {}
ref_count_lock = threading.Lock()

def change_ref_count(thread_id, add):
    ref_count_lock.acquire()
    new_ref_count = thread_id_to_ref_count.get(thread_id, 0) + add
    thread_id_to_ref_count[thread_id] = new_ref_count
    ref_count_lock.release()
    return new_ref_count
