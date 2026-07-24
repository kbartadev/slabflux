/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * SOURCE-AVAILABLE CODEBASE
 *
 * This source file is distributed under the conditions of the SLABFLUX 
 * SOURCE-AVAILABLE AND ECOSYSTEM LICENSE (the "License").
 *
 * ----------------------------------------------------------------------------
 * CRITICAL WARNING
 * ----------------------------------------------------------------------------
 * This module may execute outside standard OS mediation layers. Incorrect 
 * integration, misconfiguration, or unsafe deployment can result in:
 *
 *   • irreversible data corruption
 *   • kernel instability or panics
 *   • NIC or PCIe bus desynchronization
 *   • undefined hardware state transitions
 *   • permanent loss of system integrity
 *
 * Use only in controlled environments with full understanding of the 
 * architectural constraints and hardware implications.
 *
 * ----------------------------------------------------------------------------
 * USAGE GUIDELINES
 * ----------------------------------------------------------------------------
 * Execution, integration, and deployment by developers is permitted strictly 
 * subject to the conditional grants and structural limitations defined within 
 * the License. Please refer to the License for full terms regarding corporate 
 * deployment and replication.
 *
 * ----------------------------------------------------------------------------
 * LIMITATION OF LIABILITY
 * ----------------------------------------------------------------------------
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL THE AUTHOR OR 
 * COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, 
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 * DISCLAIMER OF WARRANTY
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * See accompanying LICENSE and NOTICE files for the integrated terms of use.
 * ============================================================================* SYSTEM SPECIFICATION: METADATA COMPILER ADVERSARIAL TEST SUITE
 * Test suite for the SlabFlux Metadata Compiler
 * ============================================================================*/
#include <gtest/gtest.h>
#define SLABFLUX_META_TESTING
#include "../../src/compiler/meta_compiler.cpp"

using namespace slabflux::compiler;

class MetadataCompilerTest : public ::testing::Test {
protected:
    TopologicalParser parser;
    
    void digest(std::string_view code) {
        parser.digest_manifold(code, true);
    }

    // Simulates the actual two-pass compilation flow across multiple "files"
    void simulate_multi_manifold(const std::vector<std::string>& manifolds) {
        for (const auto& m : manifolds) parser.discover_symbols(m);
        for (size_t i = 0; i < manifolds.size(); ++i) {
            parser.digest_manifold(manifolds[i], (i == 0));
        }
    }
};

TEST_F(MetadataCompilerTest, TestComplexInheritance) {
    // Define core framework types so the agnostic resolver can find them
    parser.discover_symbols(R"(
        namespace slabflux {
            template <int ID> struct event {};
            template <typename... T> struct extends {};
        }
    )");

    digest(R"(
        namespace slabflux::test {
            struct transport_packet {};
        }
        struct alert_event : slabflux::event<200>, slabflux::extends<slabflux::test::transport_packet> {};
    )");

    auto inheritance = parser.retrieve_inheritance();
    ASSERT_TRUE(inheritance.contains("::alert_event"));
    auto alert_bases = inheritance.at("::alert_event").bases;
    ASSERT_GE(alert_bases.size(), 2);
    EXPECT_EQ(alert_bases[0], "::slabflux::event<200>");
    EXPECT_EQ(alert_bases[1], "::slabflux::extends<::slabflux::test::transport_packet>");
}

TEST_F(MetadataCompilerTest, TestMatrixAllocatorNesting) {
    digest(R"(
        template <typename T, std::size_t BlockCount>
        class alignas(64) matrix_allocator {
            static_assert((BlockCount & (BlockCount - 1)) == 0, "Matrix { } traps");
            struct uring_harness {
                int tx_fd;
            };
        };
    )");

    auto inheritance = parser.retrieve_inheritance();
    // Verify that uring_harness correctly inherited the parent template prefix
    EXPECT_TRUE(inheritance.contains("::matrix_allocator::uring_harness"));
}

TEST_F(MetadataCompilerTest, TestAdversarialLexing) {
    // Code that attempts to "trick" the lexer with strings and comments
    digest(R"(
        namespace noise {
            struct sensitive_data {
                const char* bracket_trap = "{ }";
                const char* comment_trap = "/* struct fake_type {}; */";
            };

            /* 
               struct ignored_in_comment {}; 
            */

            // struct also_ignored {};

            template class explicit_instantiation<int>;

            class __attribute__((packed)) alignas(64) real_entity : public sensitive_data {
                void internal_logic() {
                    if (true) { std::cout << "}"; }
                    auto raw_str = R"delim( struct raw_trap {}; )delim";
                }
            };
        }
    )");

    auto inheritance = parser.retrieve_inheritance();
    auto primary = parser.retrieve_primary_types();

    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::noise::sensitive_data") != primary.end());
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::noise::real_entity") != primary.end());
    
    // The "ignored" types must not exist
    EXPECT_FALSE(inheritance.contains("::noise::ignored_in_comment"));
    EXPECT_FALSE(inheritance.contains("::noise::also_ignored"));
    EXPECT_FALSE(inheritance.contains("::noise::fake_type"));
    EXPECT_FALSE(inheritance.contains("::noise::raw_trap"));
    EXPECT_FALSE(inheritance.contains("::noise::explicit_instantiation"));
}

TEST_F(MetadataCompilerTest, TestTemplateNestedClass) {
    digest(R"(
        template <typename T, std::size_t N>
        class matrix_allocator {
            struct uring_harness {
                int fd;
            };
        };
    )");

    auto inheritance = parser.retrieve_inheritance();
    auto templates = parser.retrieve_templates();

    // matrix_allocator regisztrálva van mint template
    EXPECT_TRUE(templates.contains("::matrix_allocator"));

    // uring_harness prefixe helyes (nem globális!)
    EXPECT_TRUE(inheritance.contains("::matrix_allocator::uring_harness"));
}

TEST_F(MetadataCompilerTest, TestLeadingScopeResolution) {
    digest(R"(
        struct ::global_root_type {};
        namespace local {
            struct nested_type : ::global_root_type {};
        }
    )");

    auto inheritance = parser.retrieve_inheritance();
    EXPECT_TRUE(inheritance.contains("::global_root_type"));
    ASSERT_TRUE(inheritance.contains("::local::nested_type"));
    EXPECT_EQ(inheritance.at("::local::nested_type").bases[0], "::global_root_type");
}

TEST_F(MetadataCompilerTest, TestStaticAssertAndAttributes) {
    digest(R"(
        template <typename T>
        struct validator {
            static_assert(sizeof(T) > 0, "Error { brace in string }");
            [[maybe_unused]] int dummy;

            struct internal_node {
                alignas(128) uint8_t data[128];
            };
        };
    )");

    auto inheritance = parser.retrieve_inheritance();
    // The curly brace in the static_assert must not break the stack like in matrix_allocator
    EXPECT_TRUE(inheritance.contains("::validator::internal_node"));
}

TEST_F(MetadataCompilerTest, TestModernNamespaceSyntax) {
    // C++17 nested namespace syntax: namespace A::B
    digest(R"(
        namespace outer::inner::deep {
            struct leaf_node : base {};
        }
    )");

    auto inheritance = parser.retrieve_inheritance();
    EXPECT_TRUE(inheritance.contains("::outer::inner::deep::leaf_node"));
    
    // Prefix check
    if (inheritance.contains("::outer::inner::deep::leaf_node")) {
        auto b = inheritance.at("::outer::inner::deep::leaf_node").bases[0];
        // Mivel a base nincs az outer::inner::deep-ben, a qualify ::base-t csinál belőle
        EXPECT_EQ(b, "::base");
    }
}

TEST_F(MetadataCompilerTest, TestDeepNamespaceAndAttributeSkipping) {
    digest(R"(
        namespace outer { namespace inner {
            class alignas(128) [[nodiscard]] hardware_core : public base_node {
                void compute() {
                    if (true) { /* inner brace */ }
                }
                struct nested_telemetry {};
            };
        }}
    )");

    auto inheritance = parser.retrieve_inheritance();
    EXPECT_TRUE(inheritance.contains("::outer::inner::hardware_core"));
    EXPECT_TRUE(inheritance.contains("::outer::inner::hardware_core::nested_telemetry"));
    
    auto bases = inheritance.at("::outer::inner::hardware_core").bases;
    EXPECT_EQ(bases[0], "::base_node");
}

TEST_F(MetadataCompilerTest, TestNestedNamespaceQualification) {
    digest(R"(
        namespace slabflux::verification {
            struct industrial_rig {};
        }
        namespace slabflux::test {
            struct industrial_rig {};
            struct heavy_duty_rig : public industrial_rig {};
        }
    )");

    auto inheritance = parser.retrieve_inheritance();
    // Cross-namespace qualification should still resolve correctly
    ASSERT_TRUE(inheritance.contains("::slabflux::verification::industrial_rig"));
    ASSERT_TRUE(inheritance.contains("::slabflux::test::heavy_duty_rig"));
    EXPECT_EQ(inheritance.at("::slabflux::test::heavy_duty_rig").bases[0], "::slabflux::test::industrial_rig");
}

TEST_F(MetadataCompilerTest, TestNestedNamespaceExhaustion) {
    // Replicates the ignition_test failure scenario
    digest(R"(
        namespace slabflux::test {
            struct industrial_rig {};
        }
    )");

    auto primary = parser.retrieve_primary_types();
    auto namespaces = parser.retrieve_namespaces();
    
    EXPECT_TRUE(namespaces.contains("::slabflux::test"));
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::slabflux::test::industrial_rig") != primary.end());
}

TEST_F(MetadataCompilerTest, TestMultiPartQualification) {
    digest(R"(
        namespace slabflux {
            namespace test {
                struct base_type {};
            }
            struct derived : public test::base_type {};
        }
    )");
    auto inheritance = parser.retrieve_inheritance();
    ASSERT_TRUE(inheritance.contains("::slabflux::derived"));
    EXPECT_EQ(inheritance.at("::slabflux::derived").bases[0], "::slabflux::test::base_type");
}

TEST_F(MetadataCompilerTest, TestAnonymousNamespaceIsolation) {
    digest(R"(
        namespace {
            struct local_helper {};
        }
        struct global_type : local_helper {};
    )");

    auto primary = parser.retrieve_primary_types();
    auto inheritance = parser.retrieve_inheritance();

    // A local_helper nem kerülhet ki a globális registry-be (mert nem hivatkozható)
    EXPECT_FALSE(std::find(primary.begin(), primary.end(), "::local_helper") != primary.end());
    
    // De az öröklődésnél látszódnia kell belsőleg
    EXPECT_TRUE(inheritance.contains("::global_type"));
    EXPECT_EQ(inheritance.at("::global_type").bases[0], "::local_helper");
}

TEST_F(MetadataCompilerTest, TestExternCBlocks) {
    digest(R"(
        extern "C" {
            struct legacy_c_type { int x; };
        }
        
        namespace wrapper {
            struct modern_type : legacy_c_type {};
        }
    )");

    auto inheritance = parser.retrieve_inheritance();
    // extern "C" is a bracket block, but not a namespace!
    // The meta compiler must handle this without corrupting the namespace stack.
    EXPECT_TRUE(inheritance.contains("::legacy_c_type"));
    
    ASSERT_TRUE(inheritance.contains("::wrapper::modern_type"));
    // legacy_c_type is global, because extern "C" does not introduce a namespace scope
    EXPECT_EQ(inheritance.at("::wrapper::modern_type").bases[0], "::legacy_c_type");
}

TEST_F(MetadataCompilerTest, TestShortTypeNameCrash) {
    // This would have crashed the compiler previously due to substr(0, 7) on a 3-char string
    digest(R"(
        struct A {};
        struct B : public A {};
    )");
    auto inheritance = parser.retrieve_inheritance();
    EXPECT_TRUE(inheritance.contains("::B"));
}

TEST_F(MetadataCompilerTest, TestRegisterContextMacro) {
    digest(R"(
        namespace app {
            struct my_event {};
            struct my_context {};
            REGISTER_CONTEXT(my_event, my_context)
        }
    )");

    auto contexts = parser.retrieve_contexts();
    EXPECT_TRUE(contexts.contains("::app::my_event"));
    EXPECT_EQ(contexts.at("::app::my_event")[0], "::app::my_context");
}

// ============================================================================
// NEW: NAMESPACE PREDECLARATION & HIERARCHY DISCOVERY TESTS
// ============================================================================

TEST_F(MetadataCompilerTest, TestC17NamespaceDeconstruction) {
    // Ensures namespace A::B::C creates three distinct entries in the registry
    digest("namespace A::B::C { struct T {}; }");
    auto ns = parser.retrieve_namespaces();
    EXPECT_TRUE(ns.contains("::A"));
    EXPECT_TRUE(ns.contains("::A::B"));
    EXPECT_TRUE(ns.contains("::A::B::C"));
}

TEST_F(MetadataCompilerTest, TestNamespaceDiscoveryStability) {
    // Ensures re-opening namespaces doesn't corrupt depth or registry
    digest("namespace slabflux { struct X {}; }");
    digest("namespace slabflux { namespace test { struct Y {}; } }");
    digest("namespace slabflux::test { struct Z {}; }");

    auto ns = parser.retrieve_namespaces();
    auto primary = parser.retrieve_primary_types();

    EXPECT_TRUE(ns.contains("::slabflux"));
    EXPECT_TRUE(ns.contains("::slabflux::test"));
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::slabflux::X") != primary.end());
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::slabflux::test::Y") != primary.end());
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::slabflux::test::Z") != primary.end());
}

TEST_F(MetadataCompilerTest, TestNamespaceShadowingDiscovery) {
    // Verify that 'test' is caught as a namespace even if a type 'test_t' is nearby
    digest(R"(
        namespace slabflux {
            struct test_t {}; 
            namespace test {
                struct industrial_rig {};
            }
        }
    )");
    auto ns = parser.retrieve_namespaces();
    EXPECT_TRUE(ns.contains("::slabflux::test"));
    EXPECT_FALSE(ns.contains("::slabflux::test_t")); // Should not be in NS registry
}

TEST_F(MetadataCompilerTest, TestNamespaceHierarchyDepthDiscovery) {
    // Checks if deep nesting (10 levels) is tracked without stack drift
    digest("namespace n1::n2::n3::n4::n5::n6::n7::n8::n9::n10 { struct deep_type {}; }");
    auto ns = parser.retrieve_namespaces();
    EXPECT_TRUE(ns.contains("::n1::n2::n3::n4::n5::n6::n7::n8::n9::n10"));
    
    auto primary = parser.retrieve_primary_types();
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::n1::n2::n3::n4::n5::n6::n7::n8::n9::n10::deep_type") != primary.end());
}

TEST_F(MetadataCompilerTest, TestCrossManifoldNamespaceRegistry) {
    // Simulate discovery across different file buffers (manifolds)
    parser.digest_manifold("namespace slabflux::core { struct A {}; }", true);
    parser.digest_manifold("namespace slabflux::test { struct B : core::A {}; }", true);

    auto ns = parser.retrieve_namespaces();
    auto inheritance = parser.retrieve_inheritance();

    EXPECT_TRUE(ns.contains("::slabflux::core"));
    EXPECT_TRUE(ns.contains("::slabflux::test"));
    ASSERT_TRUE(inheritance.contains("::slabflux::test::B"));
    // Ensure 'core::A' was correctly resolved to the already discovered 'slabflux::core::A'
    EXPECT_EQ(inheritance.at("::slabflux::test::B").bases[0], "::slabflux::core::A");
}

// ============================================================================
// NEW: ADVERSARIAL NAMESPACE & DECLARATION ORDER STABILITY TESTS
// ============================================================================

TEST_F(MetadataCompilerTest, TestNamespaceSovereignty) {
    // Replicates the 'industrial_rig' failure: Same name in different sibling namespaces
    digest(R"(
        namespace slabflux {
            namespace verification { struct industrial_rig {}; }
            namespace test { struct industrial_rig {}; }
        }
    )");

    auto primary = parser.retrieve_primary_types();
    
    // Both MUST exist as distinct fully qualified entries in exact discovery order
    ASSERT_GE(primary.size(), 2);
    EXPECT_EQ(primary[0], "::slabflux::verification::industrial_rig");
    EXPECT_EQ(primary[1], "::slabflux::test::industrial_rig");
}

TEST_F(MetadataCompilerTest, TestDeclarationOrderPreservation) {
    // The generator must respect source order for deterministic CMake builds
    digest(R"(
        struct zeta_type {};
        struct alpha_type {};
        struct beta_type {};
    )");

    auto primary = parser.retrieve_primary_types();
    ASSERT_EQ(primary.size(), 3);
    EXPECT_EQ(primary[0], "::zeta_type");
    EXPECT_EQ(primary[1], "::alpha_type");
    EXPECT_EQ(primary[2], "::beta_type");
}

TEST_F(MetadataCompilerTest, TestNamespaceShellDiscoveryOrder) {
    // Verify hierarchy shells are discovered in order, not alphabetically
    digest("namespace verification {} namespace test {}");
    auto ns_order = parser.retrieve_namespace_order();
    
    ASSERT_GE(ns_order.size(), 2);
    EXPECT_EQ(ns_order[0], "::verification");
    EXPECT_EQ(ns_order[1], "::test");
}

TEST_F(MetadataCompilerTest, TestCrossNamespaceAmbiguity) {
    // Ensure 'qualify' picks the local symbol over a global/sibling one
    digest(R"(
        namespace A { struct shared {}; }
        namespace B { 
            struct shared {}; 
            struct local : shared {}; 
        }
    )");

    auto inheritance = parser.retrieve_inheritance();
    ASSERT_TRUE(inheritance.contains("::B::local"));
    // Must resolve to B::shared, not A::shared
    EXPECT_EQ(inheritance.at("::B::local").bases[0], "::B::shared");
}

TEST_F(MetadataCompilerTest, TestStrictUsingNamespaceResolution) {
    // HARDENING: Prove the compiler resolves symbols via standard C++ 'using' rules.
    // Manifold 1: Definition in a deeply nested, random project namespace.
    parser.discover_symbols("namespace org::vendor::core { struct physical_node {}; }");
    
    // Manifold 2: The test file uses the symbol unqualified.
    digest(R"(
        using namespace org::vendor::core;
        namespace app {
            struct concrete_impl : physical_node {};
        }
    )");

    auto inheritance = parser.retrieve_inheritance();
    ASSERT_TRUE(inheritance.contains("::app::concrete_impl"));
    // Must resolve to the discovered namespace, proving purely symbolic search.
    EXPECT_EQ(inheritance.at("::app::concrete_impl").bases[0], "::org::vendor::core::physical_node");
}

TEST_F(MetadataCompilerTest, TestSymbolPriorityStability) {
    // HARDENING: Prove framework types are resolved correctly via discovery.
    // 1. Discover the framework 'extends'
    parser.discover_symbols("namespace framework { template <typename T> struct extends {}; }");
    // 2. Discover a sibling pollution that would have caused a collision
    parser.discover_symbols("namespace framework::internal { namespace extends {} }");
    
    digest(R"(
        using namespace framework;
        struct base {};
        struct derived : extends<base> {};
    )");

    auto inheritance = parser.retrieve_inheritance();
    ASSERT_TRUE(inheritance.contains("::derived"));
    EXPECT_EQ(inheritance.at("::derived").bases[0], "::framework::extends<::base>");
}

TEST_F(MetadataCompilerTest, TestNamespaceSovereigntyAndOrder) {
    // Replicates the industrial_rig failure across sibling namespaces
    digest(R"(
        namespace slabflux {
            namespace verification { struct industrial_rig {}; }
            namespace test { struct industrial_rig {}; }
        }
    )");

    auto primary = parser.retrieve_primary_types();
    ASSERT_GE(primary.size(), 2);
    EXPECT_EQ(primary[0], "::slabflux::verification::industrial_rig");
    EXPECT_EQ(primary[1], "::slabflux::test::industrial_rig");
}

TEST_F(MetadataCompilerTest, TestMultiManifoldIndustrialSovereignty) {
    // HARDENING: Replicates the nightmare scenario of 150+ headers.
    // We simulate three distinct files being passed to the compiler.

    std::vector<std::string> files;

    // File 0: The Primary Target (e.g., af_xdp_ingress_test.cpp)
    // Defines a type that inherits from a core component.
    files.push_back(R"(
        namespace slabflux::test {
            struct industrial_rig : public verification::industrial_rig {
                int local_state;
            };
        }
    )");

    // File 1: A Core Header (e.g., industrial_rig_test.hpp)
    // Defines the base class that File 0 needs.
    files.push_back(R"(
        namespace slabflux::verification {
            struct industrial_rig {
                uint64_t timestamp;
            };
        }
    )");

    // File 2: A Noise Header (e.g., some_random_driver.hpp)
    // Contains a forward declaration that might shadow the definition if the registry is weak.
    files.push_back(R"(
        namespace slabflux::verification {
            struct industrial_rig; 
        }
    )");

    simulate_multi_manifold(files);

    auto inheritance = parser.retrieve_inheritance();
    auto primary = parser.retrieve_primary_types();

    // 1. The primary type must be preserved (File 2's forward declaration cannot overwrite it)
    ASSERT_TRUE(inheritance.contains("::slabflux::test::industrial_rig"));
    
    // 2. Base class resolution must succeed from File 1, despite the "noise" in File 2
    EXPECT_EQ(inheritance.at("::slabflux::test::industrial_rig").bases[0], "::slabflux::verification::industrial_rig");
}

TEST_F(MetadataCompilerTest, TestMultiManifoldTemplateResolution) {
    // HARDENING: Verify template visibility across separate manifolds.
    std::vector<std::string> files;

    // File 1: Common template library (Project-Agnostic)
    files.push_back("namespace generic { template <typename T> struct packet_wrapper {}; }");

    // File 0: Logic using that template
    files.push_back(R"(
        namespace app {
            struct telemetry_packet {};
            struct derived_packet : public generic::packet_wrapper<telemetry_packet> {};
        }
    )");

    simulate_multi_manifold(files);

    auto inheritance = parser.retrieve_inheritance();
    ASSERT_TRUE(inheritance.contains("::app::derived_packet"));
    EXPECT_EQ(inheritance.at("::app::derived_packet").bases[0], "::generic::packet_wrapper<::app::telemetry_packet>");
}

TEST_F(MetadataCompilerTest, TestMultiManifoldNamespaceReentry) {
    // HARDENING: Re-opening the same namespace across multiple files and resolving symbols.
    std::vector<std::string> files;

    // File 1: Core definition
    files.push_back("namespace slabflux::core { struct clock {}; }");

    // File 2: Extension in the same namespace but different file
    files.push_back("namespace slabflux::core { struct precision_clock : public clock {}; }");

    // File 0: App target referencing the chain
    files.push_back("namespace app { struct scheduler : public slabflux::core::precision_clock {}; }");

    simulate_multi_manifold(files);

    auto inheritance = parser.retrieve_inheritance();
    ASSERT_TRUE(inheritance.contains("::slabflux::core::precision_clock"));
    EXPECT_EQ(inheritance.at("::slabflux::core::precision_clock").bases[0], "::slabflux::core::clock");

    ASSERT_TRUE(inheritance.contains("::app::scheduler"));
    EXPECT_EQ(inheritance.at("::app::scheduler").bases[0], "::slabflux::core::precision_clock");
}

TEST_F(MetadataCompilerTest, TestGlobalAmbiguityIndustrialHardening) {
    // Reproduces the physics_test.cpp failure: global vs remote symbol.
    std::vector<std::string> files;

    // File 1: A disruptive namespace (e.g. framework header) where a base_event exists
    files.push_back("namespace slabflux::core { struct base_event { int x; }; }");

    // File 0: The test file, where base_event is a global definition!
    files.push_back(R"(
        struct base_event { uint64_t ts; };
        namespace local {
            struct derived_event : public base_event {};
        }
    )");

    simulate_multi_manifold(files);

    auto inheritance = parser.retrieve_inheritance();
    
    ASSERT_TRUE(inheritance.contains("::local::derived_event"));
    // STRICT REQUIREMENT: The resolution must be ::base_event.
    // If it used a heuristic, it would choose slabflux::core::base_event, causing a build error.
    EXPECT_EQ(inheritance.at("::local::derived_event").bases[0], "::base_event");
}

TEST_F(MetadataCompilerTest, TestPhantomNestedClassResolution) {
    // HARDENING: Ensure Pass 1 does not flatten nested structs, which would
    // create phantom types in the global registry and hijack C++ template lookups.
    std::vector<std::string> files;
    files.push_back(R"(
        namespace slabflux::core {
            struct pipeline {
                template <typename T> struct extends {}; // Nested type!
            };
        }
    )");
    files.push_back("namespace slabflux { template <typename T> struct extends {}; }");
    files.push_back(R"(
        namespace slabflux::core {
            struct base_event {};
            struct derived_event : extends<base_event> {};
        }
    )");

    simulate_multi_manifold(files);

    auto inheritance = parser.retrieve_inheritance();
    ASSERT_TRUE(inheritance.contains("::slabflux::core::derived_event"));
    // If the Pass 1 bug exists, this evaluates to ::slabflux::core::extends<...> and causes a g++ compile error
    EXPECT_EQ(inheritance.at("::slabflux::core::derived_event").bases[0], "::slabflux::extends<::slabflux::core::base_event>");
}

TEST_F(MetadataCompilerTest, TestNestedClassForwardDeclarationPrevention) {
    // HARDENING: Replicates the af_xdp_ingress_test failure.
    // Nested structs must NOT be registered as primary types because they 
    // cannot be safely forward-declared in a force-included MOC.
    digest(R"(
        struct link_orchestrator {
            struct environment_guard {
                int x;
            };
        };
    )");
    auto primary = parser.retrieve_primary_types();
    EXPECT_FALSE(std::find(primary.begin(), primary.end(), "::link_orchestrator::environment_guard") != primary.end());
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::link_orchestrator") != primary.end());
}

TEST_F(MetadataCompilerTest, TestMultiplePrimaryTargetsIntegration) {
    // HARDENING: Verify that multiple source files can act as primary targets simultaneously.
    // This strictly simulates the 'all_slabflux' unified compilation matrix where multiple 
    // .cpp files are digested and must all independently emit their local types to the MOC.
    parser.discover_symbols("struct File1Type {};");
    parser.discover_symbols("struct File2Type {};");
    
    parser.digest_manifold("struct File1Type {};", true);
    parser.digest_manifold("struct File2Type {};", true);
    
    auto primary = parser.retrieve_primary_types();
    ASSERT_GE(primary.size(), 2);
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::File1Type") != primary.end());
    EXPECT_TRUE(std::find(primary.begin(), primary.end(), "::File2Type") != primary.end());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}