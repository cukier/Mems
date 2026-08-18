import React, { useMemo } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import Svg, { Circle, Line, Polyline, Text as SvgText } from 'react-native-svg';

import type { Sample } from '../hooks/useMeshScanner';
import type { Theme } from '../theme';

interface Props {
  samples: Sample[];
  theme: Theme;
  width: number;
  height?: number;
}

const PADDING = { top: 12, right: 12, bottom: 24, left: 36 };

/** Hand-rolled SVG line chart for accel X/Y/Z (converted to g), plotted
 * against the recent sample window. Deliberately simple: this app only
 * ever needs this one chart, so a small dependency-free component beats
 * pulling in a full charting library. */
export function AccelLineChart({ samples, theme, width, height = 220 }: Props) {
  const innerWidth = Math.max(width - PADDING.left - PADDING.right, 1);
  const innerHeight = Math.max(height - PADDING.top - PADDING.bottom, 1);

  const { pointsX, pointsY, pointsZ, minG, maxG, latest } = useMemo(() => {
    const gSamples = samples.map(
      s => s.accelMg.map(mg => mg / 1000) as [number, number, number],
    );

    let min = -1;
    let max = 1;
    for (const [x, y, z] of gSamples) {
      min = Math.min(min, x, y, z);
      max = Math.max(max, x, y, z);
    }
    // Small headroom so lines don't touch the chart edges.
    const span = max - min || 1;
    min -= span * 0.1;
    max += span * 0.1;

    const n = gSamples.length;
    const xAt = (i: number) =>
      n <= 1 ? 0 : (i / (n - 1)) * innerWidth;
    const yAt = (v: number) =>
      innerHeight - ((v - min) / (max - min)) * innerHeight;

    const toPoints = (axis: 0 | 1 | 2) =>
      gSamples.map((g, i) => `${xAt(i)},${yAt(g[axis])}`).join(' ');

    return {
      pointsX: toPoints(0),
      pointsY: toPoints(1),
      pointsZ: toPoints(2),
      minG: min,
      maxG: max,
      latest: gSamples[gSamples.length - 1] ?? null,
    };
  }, [samples, innerWidth, innerHeight]);

  const gridLines = 4;

  return (
    <View>
      <Svg width={width} height={height}>
        {/* Horizontal grid lines + y-axis labels */}
        {Array.from({ length: gridLines + 1 }).map((_, i) => {
          const frac = i / gridLines;
          const y = PADDING.top + frac * innerHeight;
          const value = maxG - frac * (maxG - minG);
          return (
            <React.Fragment key={i}>
              <Line
                x1={PADDING.left}
                y1={y}
                x2={PADDING.left + innerWidth}
                y2={y}
                stroke={theme.gridLine}
                strokeWidth={1}
              />
              <SvgText
                x={PADDING.left - 6}
                y={y + 3}
                fontSize={9}
                fill={theme.axisText}
                textAnchor="end">
                {value.toFixed(1)}
              </SvgText>
            </React.Fragment>
          );
        })}

        {/* Axis line */}
        <Line
          x1={PADDING.left}
          y1={PADDING.top}
          x2={PADDING.left}
          y2={PADDING.top + innerHeight}
          stroke={theme.axisText}
          strokeWidth={1}
        />
        <Line
          x1={PADDING.left}
          y1={PADDING.top + innerHeight}
          x2={PADDING.left + innerWidth}
          y2={PADDING.top + innerHeight}
          stroke={theme.axisText}
          strokeWidth={1}
        />

        {samples.length > 1 && (
          <React.Fragment>
            <Polyline
              points={pointsX}
              fill="none"
              stroke={theme.seriesX}
              strokeWidth={2}
              strokeLinejoin="round"
              strokeLinecap="round"
              transform={`translate(${PADDING.left}, ${PADDING.top})`}
            />
            <Polyline
              points={pointsY}
              fill="none"
              stroke={theme.seriesY}
              strokeWidth={2}
              strokeLinejoin="round"
              strokeLinecap="round"
              transform={`translate(${PADDING.left}, ${PADDING.top})`}
            />
            <Polyline
              points={pointsZ}
              fill="none"
              stroke={theme.seriesZ}
              strokeWidth={2}
              strokeLinejoin="round"
              strokeLinecap="round"
              transform={`translate(${PADDING.left}, ${PADDING.top})`}
            />
          </React.Fragment>
        )}

        {samples.length === 0 && (
          <SvgText
            x={PADDING.left + innerWidth / 2}
            y={PADDING.top + innerHeight / 2}
            fontSize={12}
            fill={theme.subtleText}
            textAnchor="middle">
            Waiting for data…
          </SvgText>
        )}

        <SvgText
          x={PADDING.left + innerWidth / 2}
          y={height - 4}
          fontSize={9}
          fill={theme.axisText}
          textAnchor="middle">
          {`recent samples (up to ${samples.length}) →`}
        </SvgText>
      </Svg>

      <View style={styles.legendRow}>
        <LegendSwatch color={theme.seriesX} label="X" value={latest?.[0]} theme={theme} />
        <LegendSwatch color={theme.seriesY} label="Y" value={latest?.[1]} theme={theme} />
        <LegendSwatch color={theme.seriesZ} label="Z" value={latest?.[2]} theme={theme} />
        <Text style={[styles.unit, { color: theme.subtleText }]}>accel (g)</Text>
      </View>
    </View>
  );
}

function LegendSwatch({
  color,
  label,
  value,
  theme,
}: {
  color: string;
  label: string;
  value: number | undefined;
  theme: Theme;
}) {
  return (
    <View style={styles.legendItem}>
      <Svg width={12} height={12}>
        <Circle cx={6} cy={6} r={5} fill={color} />
      </Svg>
      <Text style={[styles.legendText, { color: theme.text }]}>
        {label}
        {value !== undefined ? `: ${value.toFixed(2)}g` : ''}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  legendRow: {
    flexDirection: 'row',
    alignItems: 'center',
    flexWrap: 'wrap',
    marginTop: 4,
    paddingHorizontal: PADDING.left,
    gap: 12,
  },
  legendItem: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 4,
  },
  legendText: {
    fontSize: 12,
    fontVariant: ['tabular-nums'],
  },
  unit: {
    fontSize: 11,
    marginLeft: 'auto',
  },
});
