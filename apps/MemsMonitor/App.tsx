/**
 * MemsMonitor
 *
 * Scans for BLE advertising packets from the flood-mesh IMU network built in
 * this repo's ESP32-C3 firmware (see src/Mems/main/mesh_net.c) and plots the
 * accel data for a chosen node as a live line chart.
 *
 * @format
 */

import React, { useEffect, useState } from 'react';
import {
  ScrollView,
  StatusBar as RNStatusBar,
  StyleSheet,
  Text,
  useColorScheme,
  useWindowDimensions,
  View,
} from 'react-native';
import { SafeAreaProvider, useSafeAreaInsets } from 'react-native-safe-area-context';

import { AccelLineChart } from './src/components/AccelLineChart';
import { NodePicker } from './src/components/NodePicker';
import { StatusBar as MeshStatusBar } from './src/components/StatusBar';
import { formatNodeId } from './src/ble/meshPacket';
import { useMeshScanner } from './src/hooks/useMeshScanner';
import { darkTheme, lightTheme } from './src/theme';

function App() {
  const isDarkMode = useColorScheme() === 'dark';

  return (
    <SafeAreaProvider>
      <RNStatusBar barStyle={isDarkMode ? 'light-content' : 'dark-content'} />
      <AppContent />
    </SafeAreaProvider>
  );
}

function AppContent() {
  const insets = useSafeAreaInsets();
  const isDarkMode = useColorScheme() === 'dark';
  const theme = isDarkMode ? darkTheme : lightTheme;
  const { width } = useWindowDimensions();

  const { scanning, error, nodes, manufacturerDataFormat } = useMeshScanner();
  const [selectedNodeId, setSelectedNodeId] = useState<number | null>(null);

  // Re-render periodically so "Xs ago" labels in the node picker stay fresh
  // even when no new packets are arriving.
  const [, forceTick] = useState(0);
  useEffect(() => {
    const id = setInterval(() => forceTick(t => t + 1), 1000);
    return () => clearInterval(id);
  }, []);

  const nodeList = Object.values(nodes).sort((a, b) => a.nodeId - b.nodeId);

  useEffect(() => {
    if (selectedNodeId === null && nodeList.length > 0) {
      setSelectedNodeId(nodeList[0].nodeId);
    }
  }, [nodeList, selectedNodeId]);

  const selectedNode =
    selectedNodeId !== null ? nodes[selectedNodeId] : undefined;

  return (
    <View style={[styles.container, { backgroundColor: theme.background, paddingTop: insets.top }]}>
      <ScrollView contentContainerStyle={styles.scrollContent}>
        <Text style={[styles.title, { color: theme.text }]}>MemsMonitor</Text>
        <Text style={[styles.subtitle, { color: theme.subtleText }]}>
          Flood-mesh IMU telemetry (company ID 0xFFFF)
        </Text>

        <View style={[styles.card, { backgroundColor: theme.card, borderColor: theme.border }]}>
          <MeshStatusBar
            scanning={scanning}
            error={error}
            nodeCount={nodeList.length}
            theme={theme}
          />
          {manufacturerDataFormat && (
            <Text style={[styles.debugLine, { color: theme.subtleText }]}>
              manufacturerData: {manufacturerDataFormat}
            </Text>
          )}
        </View>

        <Text style={[styles.sectionHeading, { color: theme.text }]}>Nodes</Text>
        <View style={[styles.card, { backgroundColor: theme.card, borderColor: theme.border }]}>
          <NodePicker
            nodes={nodeList}
            selectedNodeId={selectedNodeId}
            onSelect={setSelectedNodeId}
            theme={theme}
          />
        </View>

        <Text style={[styles.sectionHeading, { color: theme.text }]}>
          {selectedNode
            ? `Accel — node ${formatNodeId(selectedNode.nodeId)}`
            : 'Accel'}
        </Text>
        <View style={[styles.card, { backgroundColor: theme.card, borderColor: theme.border }]}>
          <AccelLineChart
            samples={selectedNode?.samples ?? []}
            theme={theme}
            width={width - styles.card.padding * 2 - styles.scrollContent.padding * 2}
          />
        </View>
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
  },
  scrollContent: {
    padding: 16,
    paddingBottom: 32,
  },
  title: {
    fontSize: 22,
    fontWeight: '800',
  },
  subtitle: {
    fontSize: 12,
    marginTop: 2,
    marginBottom: 16,
  },
  sectionHeading: {
    fontSize: 14,
    fontWeight: '700',
    marginTop: 16,
    marginBottom: 8,
  },
  card: {
    borderWidth: 1,
    borderRadius: 12,
    padding: 12,
  },
  debugLine: {
    fontSize: 10,
    marginTop: 6,
  },
});

export default App;
