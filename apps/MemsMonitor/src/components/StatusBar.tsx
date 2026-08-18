import React from 'react';
import { ActivityIndicator, StyleSheet, Text, View } from 'react-native';

import type { Theme } from '../theme';

interface Props {
  scanning: boolean;
  error: string | null;
  nodeCount: number;
  theme: Theme;
}

export function StatusBar({ scanning, error, nodeCount, theme }: Props) {
  return (
    <View style={styles.row}>
      <View style={styles.left}>
        {scanning && !error ? (
          <ActivityIndicator size="small" color={theme.accent} />
        ) : (
          <View
            style={[
              styles.dot,
              { backgroundColor: error ? theme.danger : theme.subtleText },
            ]}
          />
        )}
        <Text style={[styles.status, { color: error ? theme.danger : theme.text }]}>
          {error ?? (scanning ? 'Scanning…' : 'Starting…')}
        </Text>
      </View>
      <Text style={[styles.count, { color: theme.subtleText }]}>
        {nodeCount} node{nodeCount === 1 ? '' : 's'} discovered
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
  },
  left: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
  },
  dot: {
    width: 10,
    height: 10,
    borderRadius: 5,
  },
  status: {
    fontSize: 13,
    fontWeight: '600',
  },
  count: {
    fontSize: 12,
  },
});
