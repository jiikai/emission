var container = document.getElementById('chart-area');
var data = {
  series: [%s]
};
var options = {
    chart: {
        width: 900,
        height: 700,
        title: '%s',
        format: '0.00'
    },
    map: 'world',
    legend: {
        align: 'bottom'
    }
};
var theme = {
    series: {
        startColor: '#ffefef',
        endColor: '#ac4142',
        overColor: '#75b5aa'
    }
};
var chart = tui.chart.mapChart(container, data, options);
