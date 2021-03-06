/*
 * Copyright (c) 2016, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

 /*
 * @test TestPLABResize
 * @bug 8141278 8141141
 * @summary Test for PLAB resizing
 * @requires vm.gc=="G1" | vm.gc=="null"
 * @requires vm.opt.FlightRecorder != true
 * @library /testlibrary /../../test/lib /
 * @modules java.management
 * @build ClassFileInstaller
 *        sun.hotspot.WhiteBox
 *        gc.g1.plab.lib.LogParser
 *        gc.g1.plab.lib.MemoryConsumer
 *        gc.g1.plab.lib.PLABUtils
 *        gc.g1.plab.lib.AppPLABResize
 * @ignore 8150183
 * @run main ClassFileInstaller sun.hotspot.WhiteBox
 *                              sun.hotspot.WhiteBox$WhiteBoxPermission
 * @run main gc.g1.plab.TestPLABResize
 */
package gc.g1.plab;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import java.io.PrintStream;

import gc.g1.plab.lib.LogParser;
import gc.g1.plab.lib.PLABUtils;
import gc.g1.plab.lib.AppPLABResize;

import jdk.test.lib.OutputAnalyzer;
import jdk.test.lib.ProcessTools;

/**
 * Test for PLAB resizing.
 */
public class TestPLABResize {

    private static final int OBJECT_SIZE_SMALL = 10;
    private static final int OBJECT_SIZE_MEDIUM = 70;
    private static final int OBJECT_SIZE_HIGH = 150;
    private static final int GC_NUM_SMALL = 1;
    private static final int GC_NUM_MEDIUM = 3;
    private static final int GC_NUM_HIGH = 7;
    private static final int WASTE_PCT_SMALL = 10;
    private static final int WASTE_PCT_MEDIUM = 20;
    private static final int WASTE_PCT_HIGH = 30;

    private static final int ITERATIONS_SMALL = 3;
    private static final int ITERATIONS_MEDIUM = 5;
    private static final int ITERATIONS_HIGH = 8;

    private final static TestCase[] TEST_CASES = {
        new TestCase(WASTE_PCT_SMALL, OBJECT_SIZE_SMALL, GC_NUM_SMALL, ITERATIONS_MEDIUM),
        new TestCase(WASTE_PCT_SMALL, OBJECT_SIZE_MEDIUM, GC_NUM_HIGH, ITERATIONS_SMALL),
        new TestCase(WASTE_PCT_SMALL, OBJECT_SIZE_HIGH, GC_NUM_MEDIUM, ITERATIONS_HIGH),
        new TestCase(WASTE_PCT_MEDIUM, OBJECT_SIZE_SMALL, GC_NUM_HIGH, ITERATIONS_MEDIUM),
        new TestCase(WASTE_PCT_MEDIUM, OBJECT_SIZE_MEDIUM, GC_NUM_SMALL, ITERATIONS_SMALL),
        new TestCase(WASTE_PCT_MEDIUM, OBJECT_SIZE_HIGH, GC_NUM_MEDIUM, ITERATIONS_HIGH),
        new TestCase(WASTE_PCT_HIGH, OBJECT_SIZE_SMALL, GC_NUM_HIGH, ITERATIONS_MEDIUM),
        new TestCase(WASTE_PCT_HIGH, OBJECT_SIZE_MEDIUM, GC_NUM_SMALL, ITERATIONS_SMALL),
        new TestCase(WASTE_PCT_HIGH, OBJECT_SIZE_HIGH, GC_NUM_MEDIUM, ITERATIONS_HIGH)
    };

    public static void main(String[] args) throws Throwable {
        for (TestCase testCase : TEST_CASES) {
            testCase.print(System.out);
            List<String> options = PLABUtils.prepareOptions(testCase.toOptions());
            options.add(AppPLABResize.class.getName());
            OutputAnalyzer out = ProcessTools.executeTestJvm(options.toArray(new String[options.size()]));
            if (out.getExitValue() != 0) {
                System.out.println(out.getOutput());
                throw new RuntimeException("Exit code is not 0");
            }
            checkResults(out.getOutput(), testCase);
        }
    }

    /**
     * Checks testing results.
     * Expected results - desired PLAB size is decreased and increased during promotion to Survivor.
     *
     * @param output - VM output
     * @param testCase
     */
    private static void checkResults(String output, TestCase testCase) {
        final LogParser log = new LogParser(output);
        final Map<Long, Map<LogParser.ReportType, Map<String, Long>>> entries = log.getEntries();

        final ArrayList<Long> plabSizes = entries.entrySet()
                .stream()
                .map(item -> {
                    return item.getValue()
                            .get(LogParser.ReportType.SURVIVOR_STATS)
                            .get("actual");
                })
                .collect(Collectors.toCollection(ArrayList::new));

        // Check that desired plab size was changed during iterations.
        // It should decrease during first half of iterations
        // and increase after.
        List<Long> decreasedPlabs = plabSizes.subList(testCase.getIterations(), testCase.getIterations() * 2);
        List<Long> increasedPlabs = plabSizes.subList(testCase.getIterations() * 2, testCase.getIterations() * 3);

        Long prev = decreasedPlabs.get(0);
        for (int index = 1; index < decreasedPlabs.size(); ++index) {
            Long current = decreasedPlabs.get(index);
            if (prev < current) {
                System.out.println(output);
                throw new RuntimeException("Test failed! Expect that previous PLAB size should be greater than current. Prev.size: " + prev + " Current size:" + current);
            }
            prev = current;
        }

        prev = increasedPlabs.get(0);
        for (int index = 1; index < increasedPlabs.size(); ++index) {
            Long current = increasedPlabs.get(index);
            if (prev > current) {
                System.out.println(output);
                throw new RuntimeException("Test failed! Expect that previous PLAB size should be less than current. Prev.size: " + prev + " Current size:" + current);
            }
            prev = current;
        }

        System.out.println("Test passed!");
    }

    /**
     * Description of one test case.
     */
    private static class TestCase {

        private final int wastePct;
        private final int chunkSize;
        private final int parGCThreads;
        private final int iterations;

        /**
         * @param wastePct
         * ParallelGCBufferWastePct
         * @param chunkSize
         * requested object size for memory consumption
         * @param parGCThreads
         * -XX:ParallelGCThreads
         * @param iterations
         *
         */
        public TestCase(int wastePct,
                int chunkSize,
                int parGCThreads,
                int iterations
        ) {
            if (wastePct == 0 || chunkSize == 0 || parGCThreads == 0 || iterations == 0) {
                throw new IllegalArgumentException("Parameters should not be 0");
            }
            this.wastePct = wastePct;

            this.chunkSize = chunkSize;
            this.parGCThreads = parGCThreads;
            this.iterations = iterations;
        }

        /**
         * Convert current TestCase to List of options.
         *
         * @return
         * List of options
         */
        public List<String> toOptions() {
            return Arrays.asList("-XX:ParallelGCThreads=" + parGCThreads,
                    "-XX:ParallelGCBufferWastePct=" + wastePct,
                    "-XX:+ResizePLAB",
                    "-Dthreads=" + parGCThreads,
                    "-Dchunk.size=" + chunkSize,
                    "-Diterations=" + iterations,
                    "-XX:NewSize=16m",
                    "-XX:MaxNewSize=16m"
            );
        }

        /**
         * Print details about test case.
         */
        public void print(PrintStream out) {
            out.println("Test case details:");
            out.println("  Parallel GC buffer waste pct : " + wastePct);
            out.println("  Chunk size : " + chunkSize);
            out.println("  Parallel GC threads : " + parGCThreads);
            out.println("  Iterations: " + iterations);
        }

        /**
         * @return iterations
         */
        public int getIterations() {
            return iterations;
        }
    }
}
