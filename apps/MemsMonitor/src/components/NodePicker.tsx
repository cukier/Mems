import React from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { formatNodeId } from '../ble/meshPacket';
import type { NodeState } from '../hooks/useMeshScanner';
import type { Theme } from '../theme';

interface Props {
  nodes: NodeState[];
  selectedNodeId: number | null;
  onSelect: (nodeId: number) => void;
  theme: Theme;
}

export function NodePicker({ nodes, selectedNodeId, onSelect, theme }: Props) {
  if (nodes.length === 0) {
    return (
      <Text style={[styles.empty, { color: theme.subtleText }]}>
        No mesh nodes discovered yet.
      </Text>
    );
  }

  return (
    <View style={styles.row}>
      {nodes.map(node => {
        const selected = node.nodeId === selectedNodeId;
        return (
          <Pressable
            key={node.nodeId}
            onPress={() => onSelect(node.nodeId)}
            style={[
              styles.chip,
              {
                backgroundColor: selected ? theme.accent : theme.card,
                borderColor: selected ? theme.accent : theme.border,
              },
            ]}>
            <Text
              style={[
                styles.nodeId,
                { color: selected ? '#ffffff' : theme.text },
              ]}>
              {formatNodeId(node.nodeId)}
            </Text>
            <Text
              style={[
                styles.detail,
                { color: selected ? '#ffffffcc' : theme.subtleText },
              ]}>
              seq {node.lastSeq} · ttl {node.lastTtl}
            </Text>
            <Text
              style={[
                styles.detail,
                { color: selected ? '#ffffffcc' : theme.subtleText },
              ]}>
              {timeAgo(node.lastSeen)}
            </Text>
          </Pressable>
        );
      })}
    </View>
  );
}

function timeAgo(timestamp: number): string {
  const deltaMs = Date.now() - timestamp;
  if (deltaMs < 1000) {
    return 'just now';
  }
  if (deltaMs < 60_000) {
    return `${Math.round(deltaMs / 1000)}s ago`;
  }
  return `${Math.round(deltaMs / 60_000)}m ago`;
}

const styles = StyleSheet.create({
  row: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: 8,
  },
  chip: {
    borderWidth: 1,
    borderRadius: 10,
    paddingVertical: 8,
    paddingHorizontal: 12,
    minWidth: 96,
  },
  nodeId: {
    fontSize: 14,
    fontWeight: '700',
  },
  detail: {
    fontSize: 11,
    marginTop: 2,
  },
  empty: {
    fontSize: 13,
    fontStyle: 'italic',
  },
});
