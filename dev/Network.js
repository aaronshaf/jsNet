"use strict"

class Network {

    constructor ({learningRate=0.2, layers=[], adaptiveLR, activation="sigmoid", cost="crossEntropy"}={}) {
        this.state = "not-defined"
        this.layers = []
        this.epochs = 0
        this.iterations = 0

        this.learningRate = learningRate
        this.weightUpdateFn = NetMath[adaptiveLR ? adaptiveLR : "noAdaptiveLR"]
        this.activation = NetMath[activation]
        this.cost = NetMath[cost]

        if(layers.length) {

            switch(true) {

                case layers.every(item => Number.isInteger(item)):
                    this.layers = layers.map(size => new Layer(size))
                    this.state = "constructed"
                    this.initLayers()
                    break

                case layers.every(item => item instanceof Layer):
                    this.state = "constructed"
                    this.layers = layers
                    this.initLayers()
                    break

                case layers.every(item => item === Layer):
                    this.state = "defined"
                    this.definedLayers = layers
                    break

                default:
                    throw new Error("There was an error constructing from the layers given.")
            }
        }
    }

    initLayers (input, expected) {

        switch(this.state){

            case "initialised":
                return

            case "defined":
                this.layers = this.definedLayers.map((layer, li) => {
                    if(!li)
                        return new layer(input)

                    if(li==this.definedLayers.length-1) 
                        return new layer(expected)

                    const hidden = this.definedLayers.length-2
                    const size = input/expected > 5 ? expected + (expected + (Math.abs(input-expected))/4) * (hidden-li+1)/(hidden/2)
                                                    : input >= expected ? input + expected * (hidden-li)/(hidden/2)
                                                                        : expected + input * (hidden-li)/(hidden/2)

                    return new layer(Math.max(Math.round(size), 0))
                })
                break

            case "not-defined":
                this.layers[0] = new Layer(input)
                this.layers[1] = new Layer(Math.ceil(input/expected > 5 ? expected + (Math.abs(input-expected))/4
                                                                        : input + expected))
                this.layers[2] = new Layer(Math.ceil(expected))
                break
        }

        this.layers.forEach(this.joinLayer.bind(this))
        this.state = "initialised"
    }

    joinLayer (layer, layerIndex) {

        layer.activation = this.activation

        if(layerIndex){
            this.layers[layerIndex-1].assignNext(layer)
            layer.assignPrev(this.layers[layerIndex-1])
        }
    }

    forward (data) {

        if(this.state!="initialised"){
            throw new Error("The network layers have not been initialised.")
        }

        if(data === undefined){
            throw new Error("No data passed to Network.forward()")
        }

        if(data.length != this.layers[0].neurons.length){
            console.warn("Input data length did not match input layer neurons count.")
        }

        this.layers[0].neurons.forEach((neuron, ni) => neuron.activation = data[ni])
        this.layers.forEach((layer, li) => li && layer.forward(data))
        return this.layers[this.layers.length-1].neurons.map(n => n.activation)
    }

    backward (expected) {
        if(expected === undefined){
            throw new Error("No data passed to Network.backward()")
        }

        if(expected.length != this.layers[this.layers.length-1].neurons.length){
            console.warn("Expected data length did not match output layer neurons count.")
        }

        this.layers[this.layers.length-1].backward(expected)

        for(let layerIndex=this.layers.length-2; layerIndex>0; layerIndex--){
            this.layers[layerIndex].backward()
        }
    }

    train (dataSet, {epochs=1, callback}={}) {
        return new Promise((resolve, reject) => {
            
            if(dataSet === undefined || dataSet === null) {
                reject("No data provided")
            }

            if(this.state != "initialised"){
                this.initLayers(dataSet[0].input.length, (dataSet[0].expected || dataSet[0].output).length)
            }

            let iterationIndex = 0
            let epochsCounter = 0
            let error = 0

            const doEpoch = () => {
                this.epochs++
                iterationIndex = 0

                doIteration()               
            }

            const doIteration = () => {
                
                if(!dataSet[iterationIndex].hasOwnProperty("input") || (!dataSet[iterationIndex].hasOwnProperty("expected") && !dataSet[iterationIndex].hasOwnProperty("output"))){
                    return reject("Data set must be a list of objects with keys: 'input' and 'expected' (or 'output')")
                }

                this.resetDeltaWeights()

                const input = dataSet[iterationIndex].input
                const output = this.forward(input)
                const target = dataSet[iterationIndex].expected || dataSet[iterationIndex].output

                this.backward(target)
                this.applyDeltaWeights()

                const iterationError = this.cost(target, output)
                error += iterationError

                if(typeof callback=="function") {
                    callback({
                        iterations: this.iterations,
                        error: iterationError,
                        input
                    })
                }

                this.iterations++
                iterationIndex++

                if(iterationIndex < dataSet.length){
                    setTimeout(doIteration.bind(this), 0)
                }else {

                    epochsCounter++
                    console.log(`Epoch: ${epochsCounter} Error: ${error/100}`)

                    if(epochsCounter < epochs){
                        doEpoch()
                    }else resolve()
                }
            }

            doEpoch()
        })
    }

    test (testSet) {
        return new Promise((resolve, reject) => {

            if(testSet === undefined || testSet === null) {
                reject("No data provided")
            }

            let totalError = 0
            let testIteration = 0

            const testInput = () => {

                console.log("Testing iteration", testIteration+1, totalError/(testIteration+1)/100)

                const output = this.forward(testSet[testIteration].input)
                const target = testSet[testIteration].expected || testSet[testIteration].output

                totalError += this.cost(target, output)

                testIteration++

                if(testIteration < testSet.length)
                    setTimeout(testInput.bind(this), 0)
                else resolve(totalError/testSet.length/100)
            }
            testInput()
        })
    }


    resetDeltaWeights () {
        this.layers.forEach((layer, li) => {
            li && layer.neurons.forEach(neuron => {
                neuron.deltaWeights = neuron.weights.map(dw => 0)
            })
        })
    }

    applyDeltaWeights () {
        this.layers.forEach((layer, li) => {
            li && layer.neurons.forEach(neuron => {
                neuron.deltaWeights.forEach((dw, dwi) => {
                    neuron.weights[dwi] = this.weightUpdateFn.bind(this, neuron.weights[dwi], dw, neuron.weightGains[dwi], neuron, dwi)()
                })
                neuron.bias = this.weightUpdateFn.bind(this, neuron.bias, neuron.deltaBias, neuron.biasGain, neuron)()
            })
        })
    }

    toJSON () {
        return {
            layers: this.layers.map(layer => {
                return {
                    neurons: layer.neurons.map(neuron => {
                        return {
                            bias: neuron.bias,
                            weights: neuron.weights
                        }
                    })
                }
            })
        }
    }

    fromJSON (data) {

        if(data === undefined || data === null){
            throw new Error("No JSON data given to import.")
        }

        this.layers = data.layers.map(layer => new Layer(layer.neurons.length, layer.neurons))
        this.state = "constructed"
        this.initLayers()
    }
}

typeof window=="undefined" && (global.Network = Network) 