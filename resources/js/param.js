$(() => {
  $('.select-chart-type').on('change', () => {
    if ($('#chart-type-line').prop('selected')) {
      $('span-data-year').hide();
      $('.select-data-year').hide();
      //document.getElementById('select_year').required = 'false';
      for (let i = 1980; i < 2013; i++) {
        $('<option id="f'+i+'y" value="'+i+'">'+i+'</option>')
          .appendTo('#from-year');
        let j = i + 1;
        $('<option id="t'+j+'y"value="'+j+'">'+j+'</option>')
          .appendTo('#to-year');
      }
      $('#span-data-range').show().css('display', 'inherit');
      //document.getElementById('from_year').required = 'true';
      //document.getElementById('to_year').required = 'true';
      let select_range = $('.select-data-range').show().css('display', 'inline-flex');
      select_range.on('change', () => {
        let from = $('#from-year').val();
        let to   = $('#to-year').val();
        if (from >= to) {
          from++;
          $('#t'+from+'y').prop('selected', true);
        }
      });
      $('.search-countries').show().css('display', 'inherit');
    } else {
      $('span-data-range').hide();
      $('.search-countries').hide();
      $('.country-list').hide();
      //document.getElementById('from_year').required = 'false';
      //document.getElementById('to_year').required = 'false';
      for (let i = 1980; i <= 2014; i++) {
        $('<option id="f'+i+'y" value="'+i+'">'+i+'</option>')
          .appendTo('#select-year');
      }
      $('#span-data-year').show().css('display', 'inherit');
      $('.select-data-year').show().css('display', 'block');
      //document.getElementById('select_year').required = 'true';
    }
  });

  $('#cbox-mod').children().filter('input').on('input', function() {
    const checkedState = $(this).prop('checked');
    $('#datalist-country').children()
    .filter('.opt-cntr-type-'+$(this).attr('id').charAt(0))
    .each(function() {
      $(this).prop('disabled', !checkedState);
    });
  });

  {
    const cntrArr = document.getElementById('select-country').children;
    const ncountries = cntrArr.length;
    let prevSelected = 0;
    const srchElem = document.getElementById('cntr-srch');
    srchElem.addEventListener('input', () => {
      let srchStr = srchElem.value.toLowerCase();
      if (!srchStr.length) {
      	for (let i = 0; i < ncountries; i++) {
        	cntrArr[i].style.display = '';
      	}
      	cntrArr[0].parentElement.selectedIndex = 0;
      } else {
        let selected = false;
        for (let i = 0; i < ncountries; i++) {
          let elem = cntrArr[i];
          if (elem.disabled !== 'true') {
            if (elem.value.toLowerCase().indexOf(srchStr) === -1) {
              elem.style.display = 'none';
            } else {
              elem.style.display = '';
              if (!selected) {
                elem.parentElement.selectedIndex = i;
                selected = true;
              }
            }
          }
        }
      }
    });
  }

  document.getElementById('o-add-btn').addEventListener('click', event => {
    const count = document.getElementById('cntr-count');
    count.value = parseInt(count.value) + 1;
    const select = document.getElementById('select-country');
    const option = select.options[select.selectedIndex];
    const name = option.value;
    const id = option.id;
    const cbox = '<input name="ccode" type="checkbox" value="'+id+'" checked>';
    const icon = '<i class="fas fa-times-circle"></i>';
    const list = document.getElementById('cntr-list');
    list.insertAdjacentHTML('beforeend',
    	'<label id=lbl'+id+' class="country-list-item">' + cbox + icon + name + '</label>');
    list.lastChild.addEventListener('click', event => {
      const elem = event.target;
      const count = document.getElementById('cntr-count');
      count.value = parseInt(count.value) - 1;
      document.getElementById(elem.value.toString()).disabled = 'false';
      const label = elem.parentElement;
      label.removeChild(elem);
      label.parentElement.removeChild(label);
      event.stopPropagation();
    });
    select.selectedIndex = -1;
    option.disabled = 'true';
    document.getElementById('o-add-btn').blur();
    document.getElementById('cntr-srch').parentElement.click();
    event.stopPropagation();
  });

});
