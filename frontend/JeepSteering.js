.pragma library

function add(a,b) { return [a[0]+b[0],a[1]+b[1],a[2]+b[2]] }
function sub(a,b) { return [a[0]-b[0],a[1]-b[1],a[2]-b[2]] }
function mix(a,b,t) { return [a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t,a[2]+(b[2]-a[2])*t] }
function length(a) { return Math.sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]) }
function rotate(point,pivot,angle) {
    var v=sub(point,pivot),c=Math.cos(angle),s=Math.sin(angle)
    return add(pivot,[c*v[0]+s*v[2],v[1],-s*v[0]+c*v[2]])
}
// Solve two fixed-length constraints, following the neutral assembly branch.
// Angles are radians internally; coordinates use the same metre-scale rig as the meshes.
function solve(input,g) {
    input=Math.max(-1,Math.min(1,input))
    var dragLength=length(sub(g.passengerEnd,g.pitmanEnd))
    var tieLength=length(sub(g.driverEnd,mix(g.passengerEnd,g.pitmanEnd,g.pickupFraction)))
    var left=0,right=0,pitman=g.pitmanEnd
    function residual(a,b) {
        var l=rotate(g.driverEnd,g.driverPivot,a)
        var r=rotate(g.passengerEnd,g.passengerPivot,b)
        return [length(sub(r,pitman))-dragLength,
                length(sub(l,mix(r,pitman,g.pickupFraction)))-tieLength]
    }
    for (var step=1;step<=12;++step) {
        pitman=rotate(g.pitmanEnd,g.pitmanPivot,-input*g.pitmanTravel*Math.PI/180*step/12)
        for (var iteration=0;iteration<12;++iteration) {
            var f=residual(left,right)
            if (Math.max(Math.abs(f[0]),Math.abs(f[1]))<1e-10) break
            var eps=1e-5,fl=residual(left+eps,right),fr=residual(left,right+eps)
            var a=(fl[0]-f[0])/eps,b=(fr[0]-f[0])/eps
            var c=(fl[1]-f[1])/eps,d=(fr[1]-f[1])/eps,det=a*d-b*c
            if (Math.abs(det)<1e-9) break
            left-=(d*f[0]-b*f[1])/det
            right-=(-c*f[0]+a*f[1])/det
        }
    }
    var driver=rotate(g.driverEnd,g.driverPivot,left)
    var passenger=rotate(g.passengerEnd,g.passengerPivot,right)
    var pickup=mix(passenger,pitman,g.pickupFraction)
    return {driverAngle:left*180/Math.PI,passengerAngle:right*180/Math.PI,
            driver:driver,passenger:passenger,pitman:pitman,pickup:pickup,
            dragError:length(sub(passenger,pitman))-dragLength,
            tieError:length(sub(driver,pickup))-tieLength}
}

function tiePoints(state,g) {
    var d=sub(state.pickup,state.driver),n=Math.sqrt(d[0]*d[0]+d[2]*d[2])
    var offset=[d[2]/n*g.tieBend,0,-d[0]/n*g.tieBend]
    return [state.driver,add(mix(state.driver,state.pickup,.18),offset),
            add(mix(state.driver,state.pickup,.82),offset),state.pickup]
}
