export interface Theme {
  background: string;
  card: string;
  text: string;
  subtleText: string;
  border: string;
  accent: string;
  danger: string;
  gridLine: string;
  axisText: string;
  seriesX: string;
  seriesY: string;
  seriesZ: string;
}

// Chosen to stay legible (not pure black/white) and keep distinct hue
// separation between the three chart series in both light and dark mode.
export const lightTheme: Theme = {
  background: '#f4f5f7',
  card: '#ffffff',
  text: '#1c1e21',
  subtleText: '#5b6270',
  border: '#dde1e6',
  accent: '#2f6fed',
  danger: '#c94b4b',
  gridLine: '#e3e6ea',
  axisText: '#7a808c',
  seriesX: '#d1495b',
  seriesY: '#1f9e6b',
  seriesZ: '#2f6fed',
};

export const darkTheme: Theme = {
  background: '#14161a',
  card: '#1e2126',
  text: '#eceef1',
  subtleText: '#9aa1ac',
  border: '#2c3038',
  accent: '#5c9aff',
  danger: '#ff6b6b',
  gridLine: '#2a2e35',
  axisText: '#8790a0',
  seriesX: '#ff7a8a',
  seriesY: '#4fd399',
  seriesZ: '#7db2ff',
};
