function StripChart(W, H, target,
                    ylabel,
                    // How many seconds of history do we want?
                    history_period,
                    update_period) {
    var bottom_margin = 20;
    var left_margin = 70;
    var totalW = W;
    var totalH = H;
    var W = totalW - left_margin;
    var H = totalH - bottom_margin;

    this.update_period = update_period;
    
    this.ymin = null;
    this.ymax = null;
    
    this.svg = d3.select(target).append('svg')
        .attr('width',  totalW)
        .attr('height', totalH);

    this.graph = this.svg.append('g')
        .attr("transform", "translate(" + left_margin + ",0)");

    var xscale = d3.scaleTime().rangeRound([0, W]);
    var yscale = d3.scaleLinear().rangeRound([H, 0]);

    this.line = d3.line()
        .x(function(d) { return xscale(d.t); })
        .y(function(d) { return yscale(d.v); });

    this.xscale = xscale;
    this.yscale = yscale;
    
    this.xaxis = d3.axisBottom(this.xscale);
    this.yaxis = d3.axisLeft(this.yscale);

    // add axes
    this.graph.append('g')
        .attr('transform', 'translate(0,' + H + ')')
        .attr('class', 'xaxis')
        .call(this.xaxis);
    this.graph.append('g')
        .attr('class', 'yaxis')
        .call(this.yaxis);

    // text label for the y axis
    this.graph.append("text")
        .attr("transform", "rotate(-90)")
        .attr("y", 0 - left_margin)
        .attr("x",0 - (H / 2))
        .attr("dy", "1em")
        .style("text-anchor", "middle")
        .text(ylabel);

    this.data = [];

    this.path = this.graph.append("path")
        .attr("class", "line")
        .datum(this.data)
        .attr("fill", "none")
        .attr("stroke", "steelblue")
        .attr("stroke-linejoin", "round")
        .attr("stroke-linecap", "round")
        .attr("stroke-width", 1.5)
        .attr("d", this.line(this.data));

    this.last_datemax = null;

    this.update = function(dates, values) {
        //var datemax = Math.max(dates);
        var datemax = null;
        for (const d of dates) {
            if ((datemax == null) || (d > datemax)) {
                datemax = d;
            }
        }
        var datemin = datemax - 1000. * history_period;

        this.data = [];
        for (var i=0; i<values.length; i++) {
            this.data.push({ t: dates[i], v: values[i] });
        }
        this.xscale.domain([datemin, datemax]);
        if ((this.ymin != null) && (this.ymax != null)) {
            this.yscale.domain([this.ymin, this.ymax]);
        } else {
            vmin = Math.min(values);
            vmax = Math.max(values);
            this.yscale.domain([vmin*0.95, vmax*1.05]);
        }
        
        var duration = this.update_period;
        if (this.last_datemax == null) {
            // We received our first data.  No slow animation.
            duration = 0.0;
        }
        var t = d3.transition()
            .duration(duration)
            .ease(d3.easeLinear);

        this.graph.select(".xaxis")
            .transition(t)
            .call(this.xaxis);
        this.graph.select(".yaxis")
            .transition(t)
            .call(this.yaxis);

        if (this.last_datemax != null) {
            // Slide the line left
            dx = this.xscale(this.last_datemax) - this.xscale(datemax);
            this.path.attr('d', this.line(this.data))
                .attr('transform', 'translate(' + -dx + ')')
                .transition(t)
                .attr('transform', 'translate(0)');
            // People normally do the transition like
            // .attr('d', line(data).attr('transform',null).transition().attr('transform', ...dx...)
            // but it seems like that means the line is shifted in its
            // final state, ie, doesn't actually correspond to the axes!
        }
        this.last_datemax = datemax;
    }
}

