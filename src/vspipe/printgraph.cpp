/*
 * Copyright (c) 2021 Fredrik Mellbin
 *
 * This file is part of VapourSynth.
 *
 * VapourSynth is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * VapourSynth is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with VapourSynth; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "printgraph.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <list>
#include <map>
#include <set>

static std::string mangleNode(VSNode *node, const VSAPI *vsapi) {
  return "n" + std::to_string(reinterpret_cast<uintptr_t>(node));
}

static std::string mangleFrame(VSNode *node, int level, const VSAPI *vsapi) {
  return "s" + std::to_string(reinterpret_cast<uintptr_t>(
                   vsapi->getNodeCreationFunctionArguments(node, level)));
}

static int getMaxLevel(VSNode *node, const VSAPI *vsapi) {
  for (int i = 0; i < INT_MAX; i++) {
    if (!vsapi->getNodeCreationFunctionArguments(node, i))
      return i - 1;
  }
  return 0;
}

static std::string printVSMap(const VSMap *args, int maxPrintLength,
                              const VSAPI *vsapi) {
  int numKeys = vsapi->mapNumKeys(args);
  std::string setArgsStr;
  for (int i = 0; i < numKeys; i++) {
    const char *key = vsapi->mapGetKey(args, i);
    int numElems = vsapi->mapNumElements(args, key);

    setArgsStr += "\\n";
    setArgsStr += key;
    setArgsStr += "=";

    switch (vsapi->mapGetType(args, key)) {
    case ptInt:
      for (int j = 0; j < std::min(maxPrintLength, numElems); j++)
        setArgsStr += (j ? ", " : "") +
                      std::to_string(vsapi->mapGetInt(args, key, j, nullptr));
      if (numElems > maxPrintLength)
        setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
      break;
    case ptFloat:
      for (int j = 0; j < std::min(maxPrintLength, numElems); j++)
        setArgsStr += (j ? ", " : "") +
                      std::to_string(vsapi->mapGetFloat(args, key, j, nullptr));
      if (numElems > maxPrintLength)
        setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
      break;
    case ptData:
      for (int j = 0; j < std::min(maxPrintLength, numElems); j++)
        setArgsStr +=
            std::string(j ? ", " : "") +
            (vsapi->mapGetDataTypeHint(args, key, j, nullptr) == dtUtf8
                 ? vsapi->mapGetData(args, key, j, nullptr)
                 : ("[binary data " +
                    std::to_string(
                        vsapi->mapGetDataSize(args, key, j, nullptr)) +
                    " bytes]"));
      if (numElems > maxPrintLength)
        setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
      break;
    case ptVideoNode:
      for (int j = 0; j < std::min(maxPrintLength, numElems); j++) {
        VSNode *ref = vsapi->mapGetNode(args, key, j, nullptr);
        const VSVideoInfo *vi = vsapi->getVideoInfo(ref);
        char formatName[32];
        vsapi->getVideoFormatName(&vi->format, formatName);
        setArgsStr += (j ? ", [" : " [") + std::string(formatName) + ":" +
                      std::to_string(vi->width) + "x" +
                      std::to_string(vi->height) + "]";
        vsapi->freeNode(ref);
      }
      if (numElems > maxPrintLength)
        setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
      break;
    case ptAudioNode:
      for (int j = 0; j < std::min(maxPrintLength, numElems); j++) {
        VSNode *ref = vsapi->mapGetNode(args, key, j, nullptr);
        const VSAudioInfo *ai = vsapi->getAudioInfo(ref);
        char formatName[32];
        vsapi->getAudioFormatName(&ai->format, formatName);
        setArgsStr += (j ? ", [" : " [") + std::string(formatName) + ":" +
                      std::to_string(ai->sampleRate) + ":" +
                      std::to_string(ai->format.channelLayout) + "]";
        vsapi->freeNode(ref);
      }
      if (numElems > maxPrintLength)
        setArgsStr += ", <" + std::to_string(numElems - maxPrintLength) + ">";
      break;
    default:
      setArgsStr += "<" + std::to_string(numElems) + ">";
    }
  }
  return setArgsStr;
}

static void
printNodeGraphHelper(bool simple, std::set<std::string> &lines,
                     std::map<std::string, std::set<std::string>> &nodes,
                     std::set<VSNode *> &visited, VSNode *node,
                     const VSAPI *vsapi) {
  if (!visited.insert(node).second)
    return;

  int maxLevel = getMaxLevel(node, vsapi);
  // how to detect groups of dependencies now? go through the graph and lump
  // them all in by the same vsapi->getNodeCreationFunctionArguments(node, 0)
  // pointer?

  std::string setArgsStr =
      printVSMap(vsapi->getNodeCreationFunctionArguments(node, 0), 5, vsapi);

  std::string thisNode = mangleNode(node, vsapi);
  std::string thisFrame = mangleFrame(node, 0, vsapi);
  std::string baseFrame = mangleFrame(node, maxLevel, vsapi);

  nodes[simple ? baseFrame : thisFrame].insert(
      "label=\"" +
      std::string(
          vsapi->getNodeCreationFunctionName(node, simple ? maxLevel : 0)) +
      setArgsStr + "\"");
  nodes[simple ? baseFrame : thisFrame].insert(
      thisNode + " [label=\"" + std::string(vsapi->getNodeName(node)) +
      "\", shape=oval]");

  int numDeps = vsapi->getNumNodeDependencies(node);
  const VSFilterDependency *deps = vsapi->getNodeDependencies(node);

  for (int i = 0; i < numDeps; i++) {
    lines.insert(mangleNode(deps[i].source, vsapi) + " -> " + thisNode);
    printNodeGraphHelper(simple, lines, nodes, visited, deps[i].source, vsapi);
  }
}

std::string printNodeGraph(bool simple, VSNode *node, const VSAPI *vsapi) {
  std::map<std::string, std::set<std::string>> nodes;
  std::set<std::string> lines;
  std::set<VSNode *> visited;
  std::string s = "digraph {\n";
  printNodeGraphHelper(simple, lines, nodes, visited, node, vsapi);
  for (const auto &iter : nodes) {
    if (iter.second.size() > 1) {
      s += "  subgraph cluster_" + iter.first + " {\n";
      for (const auto &iter2 : iter.second)
        s += "    " + iter2 + "\n";
      s += "  }\n";
    } else if (iter.second.size() <= 1) {
      for (const auto &iter2 : iter.second)
        s += "  " + iter2 + "\n";
    }
  }

  for (const auto &iter : lines)
    s += "  " + iter + "\n";
  s += "}";
  return s;
}

struct NodeTimeRecord {
  std::string filterName;
  int filterMode;
  int64_t nanoSeconds;
  std::string creationFunction;
  std::list<NodeTimeRecord> &children;

  bool operator<(const NodeTimeRecord &other) const noexcept {
    return nanoSeconds > other.nanoSeconds;
  }
};

static void printNodeTimesHelper(std::list<NodeTimeRecord> &lines,
                                 std::set<VSNode *> &visited, VSNode *node,
                                 const VSAPI *vsapi) {
  if (!visited.insert(node).second)
    return;

  int numDeps = vsapi->getNumNodeDependencies(node);
  const VSFilterDependency *deps = vsapi->getNodeDependencies(node);
  std::list<NodeTimeRecord> children;

  for (int i = 0; i < numDeps; i++) {
    printNodeTimesHelper(children, visited, deps[i].source, vsapi);
  }

  lines.push_back(
      NodeTimeRecord{vsapi->getNodeName(node), vsapi->getNodeFilterMode(node),
                     vsapi->getNodeFilterTime(node),
                     vsapi->getNodeCreationFunctionName(node, 0), children});
}

static std::string extendStringRight(const std::string &s, size_t length) {
  if (s.length() >= length)
    return s;
  else
    return s + std::string(length - s.length(), ' ');
}

static std::string extendStringLeft(const std::string &s, size_t length) {
  if (s.length() >= length)
    return s;
  else
    return std::string(length - s.length(), ' ') + s;
}

static std::string printWithTwoDecimals(double d) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.2f", d);
  return buffer;
}

static std::string filterModeToString(int fm) {
  if (fm == fmParallel)
    return "parallel";
  else if (fm == fmParallelRequests)
    return "parreq";
  else if (fm == fmUnordered)
    return "unordered";
  else if (fm == fmFrameState)
    return "fstate";
  else
    return "unordered";
}

static int get_max_depth(std::list<NodeTimeRecord> &lines, int depth) {
  int max_depth = depth;
  for (const auto &it : lines) {
    int new_depth = get_max_depth(it.children, depth + 1);
    if (new_depth > depth) {
      max_depth = new_depth;
    }
  }
  return max_depth;
}

static void print_lines_tree(std::list<NodeTimeRecord> &lines, std::string &s,
                             double processingTime, int max_depth, int depth) {
  for (const auto &it : lines) {
    print_lines_tree(it.children, s, processingTime, max_depth, depth + 1);

    for (int i = 0; i < (max_depth - depth); i++) {
      s += "--";
    }
    int indent = 60 - (max_depth - depth) * 2;
    if (indent < int(it.filterName.length())) {
      indent = it.filterName.length();
    }
    s += extendStringRight(it.filterName, indent) + " " +
         extendStringRight(filterModeToString(it.filterMode), 10) + " " +
         extendStringRight(it.creationFunction, 30) + " " +
         extendStringLeft(printWithTwoDecimals((it.nanoSeconds) /
                                               (processingTime * 10000000)),
                          10) +
         " " +
         extendStringLeft(printWithTwoDecimals(it.nanoSeconds / 1000000000.),
                          10) +
         "\n";
  }
}

std::string printNodeTimes(VSNode *node, double processingTime,
                           const VSAPI *vsapi) {
  std::list<NodeTimeRecord> lines;
  std::set<VSNode *> visited;
  std::string s;

  printNodeTimesHelper(lines, visited, node, vsapi);

  //   lines.sort();

  s += extendStringRight("Filter name", 60) + " " +
       extendStringRight("Filter mode", 10) + " " +
       extendStringRight("Creation function", 30) + " " +
       extendStringLeft("Time (%)", 10) + " " +
       extendStringLeft("Time (s)", 10) + "\n";

  int max_depth = get_max_depth(lines, 0);
  print_lines_tree(lines, s, processingTime, max_depth, 0);

  return s;
}
