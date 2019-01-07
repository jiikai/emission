var container = document.getElementById('chart-area');
var data = {
    // Substitute range of data in years:
    categories: [%s],
    // Substitute (country/area) name and (population/emission) data entries
    series: [%s]
};
var options = {
    chart: {
        width: 1160,
        height: 540,
        title: '%s' // Substitute chart title string
    },
    // Substitute name descriptive of data and the unit of measurement
    yAxis: {
        title: '%s',
        pointOnColumn: true
    },
    xAxis: {
        title: 'Year',
        type: 'datetime',
        dateFormat: 'YYYY'
    },
    series: {
        showDot: false,
        zoomable: true
    },
    tooltip: {
    // Substitute an empty string for population data and 'kt' for emissions
        suffix: '%s'
    }
};
var theme = {
    series: {
        colors: [
            '#83b14e', '#458a3f', '#295ba0', '#2a4175', '#289399',
            '#289399', '#617178', '#8a9a9a', '#516f7d', '#dddddd'
        ]
    }
};
// For apply theme
// tui.chart.registerTheme('myTheme', theme);
// options.theme = 'myTheme';
var chart = tui.chart.lineChart(container, data, options);
