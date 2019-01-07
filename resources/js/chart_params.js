$(document).ready(() => {

  $('.select-data-type').on('change', () => {
    $('.select-chart-type').show().on('change', () => {
    	if ($('#chart-type-line').prop('selected')) {
      	for (let i = 1980; i < 2013; i++) {
	  		  $('<option id="f'+i+'y" value="'+i+'">'+i+'</option>')
            .appendTo('#from-year');
          let j = i + 1;
          $('<option id="t'+j+'y"value="'+j+'">'+j+'</option>')
            .appendTo('#to-year');
  			}
    		$('.select-data-range').show().on('change', () => {
        	let from = $('#from-year').val();
          let to   = $('#to-year').val();
          if (from >= to) {
          	from++;
          	$('#t'+from+'y').prop('selected', true);
          }
      	});
        $('.search-countries').show();
      } else {
      	for (let i = 1980; i <= 2014; i++) {
        	$('<option id="f'+i+'y" value="'+i+'">'+i+'</option>')
            .appendTo('#select-year');
        }
        $('.select-data-year').show();
      }
    });
  });

  $('#incl-cont').children().on('click', function() {
  	const checkedState = $(this).prop('checked');
    $('#datalist-country').children()
    .filter('.opt-cntr-type-'+$(this).attr('id').charAt(0))
    .each(function() {
      $(this).prop('disabled', !checkedState);
    });
  });

  $('#o-add-btn').on('click', () => {
    let count = $('#cntr-count');
    count.val(parseInt(count.val()) + 1);
    const srch = $('#cntr-srch');
    const name = srch.val();
    const opt = $('#datalist-country option[value="'+name+'"]');
    const code = opt.attr('id');
    const cbox = '<input name="ccode" type="checkbox" value="'+code+'" checked />';
    const icon = '<i class="fas fa-times-circle"></i>';
    $('<div><label class="delete-checkbox">'+cbox+icon+name+'</label></div>')
      .appendTo('.country-list');
    srch.val('');
    opt.prop('disabled', true);
  });

  $('#a-add-btn').on('click', () => {
    let count = parseInt($('#cntr-count').val());
    $('#datalist-country').children().each(function() {
      if (!($(this).prop('disabled'))) {
        const code = $(this).attr('id');
        const name = $(this).val();
        const cbox = '<input name="ccode" type="checkbox" value="'+code+'" checked />';
        const icon = '<i class="fas fa-times-circle"></i>';
        $('<div><label class="delete-checkbox">'+cbox+icon+name+'</label></div>')
          .appendTo('.country-list');
        $(this).prop('disabled', true);
        ++count;
      }
    });
    $('#cntr-count').val(count);
  });

  $('.country-list').on('click', 'input', function() {
    let count = $('#cntr-count');
    count.val(parseInt(count.val()) - 1);
    $('#'+($(this).val())+'').prop('disabled', false);
    $(this).parent().remove();
    $(this).remove();
  });

});
