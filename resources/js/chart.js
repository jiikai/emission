/*!
 * verge 1.9.1+201402130803
 * https://github.com/ryanve/verge
 * MIT License 2013 Ryan Van Etten
 */
!function(a,b,c){"undefined"!=typeof module&&module.exports?module.exports=c():a[b]=c()}(this,"verge",function(){function a(){return{width:k(),height:l()}}function b(a,b){var c={};return b=+b||0,c.width=(c.right=a.right+b)-(c.left=a.left-b),c.height=(c.bottom=a.bottom+b)-(c.top=a.top-b),c}function c(a,c){return a=a&&!a.nodeType?a[0]:a,a&&1===a.nodeType?b(a.getBoundingClientRect(),c):!1}function d(b){b=null==b?a():1===b.nodeType?c(b):b;var d=b.height,e=b.width;return d="function"==typeof d?d.call(b):d,e="function"==typeof e?e.call(b):e,e/d}var e={},f="undefined"!=typeof window&&window,g="undefined"!=typeof document&&document,h=g&&g.documentElement,i=f.matchMedia||f.msMatchMedia,j=i?function(a){return!!i.call(f,a).matches}:function(){return!1},k=e.viewportW=function(){var a=h.clientWidth,b=f.innerWidth;return b>a?b:a},l=e.viewportH=function(){var a=h.clientHeight,b=f.innerHeight;return b>a?b:a};return e.mq=j,e.matchMedia=i?function(){return i.apply(f,arguments)}:function(){return{}},e.viewport=a,e.scrollX=function(){return f.pageXOffset||h.scrollLeft},e.scrollY=function(){return f.pageYOffset||h.scrollTop},e.rectangle=c,e.aspect=d,e.inX=function(a,b){var d=c(a,b);return!!d&&d.right>=0&&d.left<=k()},e.inY=function(a,b){var d=c(a,b);return!!d&&d.bottom>=0&&d.top<=l()},e.inViewport=function(a,b){var d=c(a,b);return!!d&&d.bottom>=0&&d.right>=0&&d.top<=l()&&d.left<=k()},e});

/*! end verge.js */

const chart_type = '%s';

function setChartData(arg) {
  if (chart_type !== 'map') {
    arg.categories = JSON.parse('[%s]');
  }
  arg.series = JSON.parse('[%s]');
}

(() => {
  let theme = {
    chart: {
          fontFamily: 'Fira Sans',
          background: {
              color: 'rgb(255,255,255)',
              opacity: 0
          },
      },
      title: {
        fontSize: 14,
        fontFamily: 'Fira Sans',
        fontWeight: 'bold',
        color: 'black'
      },
      legend: {
        label: {
          fontSize: 11,
          fontFamily: 'Fira Sans',
        }
      },
  };
  if (chart_type === 'map') {
    theme.series = {
      startColor: '#ffefef',
      endColor: '#ac4142',
      overColor: '#75b5aa'
    };
  }
  tui.chart.registerTheme('emission', theme);
})();

let width = verge.viewportW();
let vwh = verge.viewportH();
let height = width / 2;
let options = {
  chart: {
    format: '1,000',
    height: height < 300 ? 300 : height,
    title: '%s',
    width: width < 300 ? 300 : width
  },
  tooltip: {
    suffix: '%s'
  },
  legend: {
    align: (width > 1000 && chart_type === 'line'
            ? 'right' : chart_type === 'map'
            ? 'left' : 'bottom')
  }
};
if (chart_type === 'line') {
  options.yAxis = {
    title: '%s'
  };
  options.xAxis = {
    title: 'Year',
    type: 'dateTime',
    dateFormat: 'YYYY'
  };
  options.series = {
    showdot: false,
    spline: true,
    zoomable: true
  };
} else if (chart_type === 'map') {
  options.map = 'world';
}
options.theme = 'emission';

setChartData(window);
var chart;
if (chart_type === 'map') {
  var data = {
    "series": series
  };
  chart = tui.chart.mapChart(document.getElementById('chart-area'), data, options);
} else {
  var data = {
    "categories": categories,
    "series": series
  };
  chart = tui.chart.lineChart(document.getElementById('chart-area'), data, options);
  let not_found_msg = '%s';
  if (not_found_msg.length) {
    let not_found_elem = document.getElementById('not-found-msg');
    not_found_elem.textContent = not_found_msg;
    not_found_elem.style.visibility = "visible";
  }
}

window.addEventListener('resize', () => {
  let newW = verge.viewportW();
  if (newW < 300) {
    newW = 300;
  }
  let newH = newW / 2;
  let dimension = {
    width: newW,
    height: newH < 300 ? 300 : newH
  };
  chart.resize(dimension);
});
