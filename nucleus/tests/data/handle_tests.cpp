#include "scope/context_full.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN
SCENARIO("HandleTable::IndexList meets constraints", "[handles]") {

    GIVEN("A simple index list") {
        struct TestEntry : public data::handleImpl::EntryBase {
            uint32_t testData{0};
        };
        data::handleImpl::IndexList<TestEntry> indexList;

        WHEN("Allocating first handle") {
            THEN("Increment is of expected size") {
                REQUIRE(data::handleImpl::INITIAL_HANDLE_CAPACITY == indexList.getIncrementSize(0));
            }
            AND_WHEN("First handle is allocated") {
                auto &entry = indexList.alloc();
                THEN("Handle index is colored") {
                    REQUIRE(data::handleImpl::HANDLE_GEN_INC == entry.check);
                }
                THEN("Links are as expected (self linked)") {
                    REQUIRE(0 == entry.next);
                    REQUIRE(0 == entry.prev);
                }
                THEN("Handle table is not resized") {
                    REQUIRE(
                        0 == indexList.getIncrementSize(data::handleImpl::INITIAL_HANDLE_CAPACITY));
                }
                AND_WHEN("Second handle is allocated") {
                    auto &entry2 = indexList.alloc();
                    THEN("Second handle index is as expected") {
                        REQUIRE(data::handleImpl::HANDLE_GEN_INC + 1 == entry2.check);
                    }
                    THEN("Second links are as expected (self linked)") {
                        REQUIRE(1 == entry2.next);
                        REQUIRE(1 == entry2.prev);
                    }
                }
            }
        }
        WHEN("Three handles have been allocated") {
            auto &entry1 = indexList.alloc();
            auto &entry2 = indexList.alloc();
            auto &entry3 = indexList.alloc();
            THEN("All three handles have expected values") {
                REQUIRE(data::handleImpl::HANDLE_GEN_INC == entry1.check);
                REQUIRE(data::handleImpl::HANDLE_GEN_INC + 1 == entry2.check);
                REQUIRE(data::handleImpl::HANDLE_GEN_INC + 2 == entry3.check);
            }
            AND_WHEN("Handles are added to end of linked list") {
                data::handleImpl::LinkEntry ctrl;
                indexList.insertLast(ctrl, entry1.check);
                indexList.insertLast(ctrl, entry2.check);
                indexList.insertLast(ctrl, entry3.check);
                THEN("Linked list is formed correctly") {
                    REQUIRE(ctrl.next == 0);
                    REQUIRE(entry1.next == 1);
                    REQUIRE(entry2.next == 2);
                    REQUIRE(entry3.next == data::handleImpl::INVALID_INDEX);
                    REQUIRE(ctrl.prev == 2);
                    REQUIRE(entry3.prev == 1);
                    REQUIRE(entry2.prev == 0);
                    REQUIRE(entry1.prev == data::handleImpl::INVALID_INDEX);
                }
                AND_WHEN("Handle removed from end of linked list") {
                    indexList.unlink(ctrl, entry3.check);
                    THEN("Linked list is updated correctly") {
                        REQUIRE(ctrl.next == 0);
                        REQUIRE(entry1.next == 1);
                        REQUIRE(entry2.next == data::handleImpl::INVALID_INDEX);
                        REQUIRE(ctrl.prev == 1);
                        REQUIRE(entry2.prev == 0);
                        REQUIRE(entry1.prev == data::handleImpl::INVALID_INDEX);
                        REQUIRE(entry3.next == 2);
                        REQUIRE(entry3.prev == 2);
                    }
                }
                AND_WHEN("Handle removed from middle of linked list") {
                    indexList.unlink(ctrl, entry2.check);
                    THEN("Linked list is updated correctly") {
                        REQUIRE(ctrl.next == 0);
                        REQUIRE(entry1.next == 2);
                        REQUIRE(entry3.next == data::handleImpl::INVALID_INDEX);
                        REQUIRE(ctrl.prev == 2);
                        REQUIRE(entry3.prev == 0);
                        REQUIRE(entry1.prev == data::handleImpl::INVALID_INDEX);
                        REQUIRE(entry2.next == 1);
                        REQUIRE(entry2.prev == 1);
                    }
                }
            }
            AND_WHEN("Handles are added to start of linked list") {
                data::handleImpl::LinkEntry ctrl;
                indexList.insertFirst(ctrl, entry1.check);
                indexList.insertFirst(ctrl, entry2.check);
                indexList.insertFirst(ctrl, entry3.check);
                THEN("Linked list is formed correctly") {
                    REQUIRE(ctrl.prev == 0);
                    REQUIRE(entry1.prev == 1);
                    REQUIRE(entry2.prev == 2);
                    REQUIRE(entry3.prev == data::handleImpl::INVALID_INDEX);
                    REQUIRE(ctrl.next == 2);
                    REQUIRE(entry3.next == 1);
                    REQUIRE(entry2.next == 0);
                    REQUIRE(entry1.next == data::handleImpl::INVALID_INDEX);
                }
                AND_WHEN("Handle removed from start of linked list") {
                    indexList.unlink(ctrl, entry3.check);
                    THEN("Linked list is updated correctly") {
                        REQUIRE(ctrl.prev == 0);
                        REQUIRE(entry1.prev == 1);
                        REQUIRE(entry2.prev == data::handleImpl::INVALID_INDEX);
                        REQUIRE(ctrl.next == 1);
                        REQUIRE(entry2.next == 0);
                        REQUIRE(entry1.next == data::handleImpl::INVALID_INDEX);
                        REQUIRE(entry3.next == 2);
                        REQUIRE(entry3.prev == 2);
                    }
                }
                AND_WHEN("All handles removed from linked list") {
                    indexList.unlink(ctrl, entry2.check);
                    indexList.unlink(ctrl, entry1.check);
                    indexList.unlink(ctrl, entry3.check);
                    THEN("Linked list is updated correctly") {
                        REQUIRE(ctrl.next == data::handleImpl::INVALID_INDEX);
                        REQUIRE(ctrl.prev == data::handleImpl::INVALID_INDEX);
                        REQUIRE(entry1.next == 0);
                        REQUIRE(entry1.prev == 0);
                        REQUIRE(entry2.next == 1);
                        REQUIRE(entry2.prev == 1);
                        REQUIRE(entry3.next == 2);
                        REQUIRE(entry3.prev == 2);
                    }
                }
            }
            AND_WHEN("When handle has been freed") {
                bool didFree = indexList.free(entry2.check);
                THEN("Free did succeed") {
                    REQUIRE(didFree);
                }
                AND_WHEN("Another handle has been allocated") {
                    auto &entry4 = indexList.alloc();
                    THEN("Allocated handle did not reuse the freed handle") {
                        REQUIRE(data::handleImpl::HANDLE_GEN_INC + 3 == entry4.check);
                    }
                }
                AND_WHEN("Many handles have been allocated and freed") {
                    // In this case, there is minimal pressure on the handles, so handles
                    // can get re-used
                    for(uint32_t i = 3; i < data::handleImpl::INITIAL_HANDLE_CAPACITY - 1; ++i) {
                        auto &e = indexList.alloc();
                        indexList.free(e.check);
                    }
                    THEN("Handle table is still not resized") {
                        REQUIRE(
                            0
                            == indexList.getIncrementSize(
                                data::handleImpl::INITIAL_HANDLE_CAPACITY));
                    }
                }
                AND_WHEN("Enough handles have been allocated and free'd") {
                    // The algorithm prefers new handles over re-using handles as long as
                    // the new handles does not result in increasing the allocation
                    for(uint32_t i = 3; i < data::handleImpl::INITIAL_HANDLE_CAPACITY; ++i) {
                        auto &e = indexList.alloc();
                        indexList.free(e.check);
                    }
                    THEN("Next allocate pulls from head of free list") {
                        auto &entry5 = indexList.alloc();
                        REQUIRE(1 == (entry5.check & data::handleImpl::HANDLE_INDEX_MASK));
                        AND_THEN("Handle is colored to give it a unique value") {
                            REQUIRE((1 + 2 * data::handleImpl::HANDLE_GEN_INC) == entry5.check);
                        }
                    }
                    THEN("Free list is used in order") {
                        bool didFree3 = indexList.free(entry3.check);
                        REQUIRE(didFree3 == true);
                        auto &entry5 = indexList.alloc();
                        auto &entry6 = indexList.alloc();
                        REQUIRE((1 + 2 * data::handleImpl::HANDLE_GEN_INC) == entry5.check);
                        // other handles were free'd before entry3
                        REQUIRE((3 + 2 * data::handleImpl::HANDLE_GEN_INC) == entry6.check);
                    }
                }
            }
        }
        WHEN("Many handles have been allocated") {
            // Allocating without freeing puts pressure on the handle table, so algorithm
            // will allocate more space to relieve that pressure. This is done at the time
            // of the next alloc after all the existing capacity has been used
            for(uint32_t i = 0; i < data::handleImpl::INITIAL_HANDLE_CAPACITY; ++i) {
                indexList.alloc();
            }
            THEN("Handle table was resized") {
                auto &nextEntry = indexList.alloc();
                REQUIRE(
                    (data::handleImpl::INITIAL_HANDLE_CAPACITY + data::handleImpl::HANDLE_GEN_INC)
                    == nextEntry.check);
            }
        }
    }
}

SCENARIO("HandleTable manages handles", "[handles]") {
    GIVEN("A handle table") {
        data::HandleTable handleTable;
        WHEN("Allocating a root") {
            data::RootHandle root = handleTable.createRoot();
            THEN("Root is valid") {
                REQUIRE(root);
            }
            THEN("Root is first possible handle") {
                REQUIRE(
                    data::handleImpl::HANDLE_GEN_INC
                    == data::IdObfuscator::deobfuscate(root.asInt()));
            }
            AND_WHEN("Releasing a root") {
                [[maybe_unused]] auto p = root.partial();
                handleTable.releaseRoot(root);
                THEN("Root handle is no longer valid") {
                    REQUIRE_FALSE(root);
                }
            }
        }
        WHEN("Allocating two roots") {
            data::RootHandle root1 = handleTable.createRoot();
            data::RootHandle root2 = handleTable.createRoot();
            THEN("Both roots are valid") {
                REQUIRE(root1);
                REQUIRE(root2);
            }
            THEN("Roots are independent of each other") {
                REQUIRE(root1.asInt() != root2.asInt());
            }
        }
        WHEN("Allocating handles to a root") {
            auto root = handleTable.createRoot();
            auto obj1 = scope::makeObject<data::SharedStruct>();
            auto obj2 = scope::makeObject<data::SharedStruct>();
            auto handle1 = handleTable.create(obj1, root);
            auto handle2 = handleTable.create(obj2, root);
            THEN("Both handles are valid") {
                REQUIRE(handle1);
                REQUIRE(handle2);
                REQUIRE(obj1 == handleTable.tryGet(handle1));
                REQUIRE(obj2 == handleTable.tryGet(handle2));
            }
            THEN("Handles are independent of each other") {
                REQUIRE(handle1.asInt() != handle2.asInt());
            }
            AND_WHEN("Releasing root") {
                handleTable.releaseRoot(root);
                THEN("Handles are no longer valid") {
                    REQUIRE_FALSE(handleTable.tryGet(handle1));
                    REQUIRE_FALSE(handleTable.tryGet(handle2));
                }
            }
            AND_WHEN("Releasing individual handle") {
                handleTable.release(handle1);
                THEN("Specified handle is no longer valid") {
                    REQUIRE_FALSE(handleTable.tryGet(handle1));
                    REQUIRE(handleTable.tryGet(handle2));
                }
            }
        }
    }
}

// NOLINTEND
