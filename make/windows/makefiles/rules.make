#
# Copyright (c) 2003, 2014, Oracle and/or its affiliates. All rights reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.
#  
#

# These are the commands used externally to compile and run.
# The \ are used here for traditional Windows apps and " quoted to get
# past the Unix-like shell:
!ifdef BootStrapDir
RUN_JAVA="$(BootStrapDir)\bin\java"
RUN_JAVAP="$(BootStrapDir)\bin\javap"
RUN_JAVAH="$(BootStrapDir)\bin\javah"
RUN_JAR="$(BootStrapDir)\bin\jar"
COMPILE_JAVAC="$(BootStrapDir)\bin\javac" $(BOOTSTRAP_JAVAC_FLAGS)
COMPILE_RMIC="$(BootStrapDir)\bin\rmic"
BOOT_JAVA_HOME=$(BootStrapDir)
!else
RUN_JAVA=java
RUN_JAVAP=javap
RUN_JAVAH=javah
RUN_JAR=jar
COMPILE_JAVAC=javac $(BOOTSTRAP_JAVAC_FLAGS)
COMPILE_RMIC=rmic
BOOT_JAVA_HOME=
!endif

# Settings for javac
JAVAC_FLAGS=-g -encoding ascii

# Prefer BOOT_JDK_SOURCETARGET if it's set (typically by the top build system)
# Fall back to the values here if it's not set (hotspot only builds)
!ifndef BOOT_JDK_SOURCETARGET
BOOTSTRAP_SOURCETARGET=-source 8 -target 8
!else
BOOTSTRAP_SOURCETARGET=$(BOOT_JDK_SOURCETARGET)
!endif

BOOTSTRAP_JAVAC_FLAGS=$(JAVAC_FLAGS) $(BOOTSTRAP_SOURCETARGET)

# VS2012 and VS2013 loads VS10 projects just fine (and will
# upgrade them automatically to VS2012 format).
VcVersion=VC10
ProjectFile=jvm.vcxproj

